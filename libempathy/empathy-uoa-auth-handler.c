/*
 * empathy-auth-uoa.c - Source for Uoa SASL authentication
 * Copyright (C) 2012 Collabora Ltd.
 * @author Xavier Claessens <xavier.claessens@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"

#include <libaccounts-glib/ag-account.h>
#include <libaccounts-glib/ag-account-service.h>
#include <libaccounts-glib/ag-auth-data.h>
#include <libaccounts-glib/ag-manager.h>
#include <libaccounts-glib/ag-service.h>

#include <libsignon-glib/signon-identity.h>
#include <libsignon-glib/signon-auth-session.h>

#define DEBUG_FLAG EMPATHY_DEBUG_SASL
#include "empathy-debug.h"
#include "empathy-utils.h"
#include "empathy-uoa-auth-handler.h"
#include "empathy-uoa-utils.h"
#include "empathy-sasl-mechanisms.h"

struct _EmpathyUoaAuthHandlerPriv
{
  AgManager *manager;
};

G_DEFINE_TYPE (EmpathyUoaAuthHandler, empathy_uoa_auth_handler, G_TYPE_OBJECT);

static void
empathy_uoa_auth_handler_init (EmpathyUoaAuthHandler *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_UOA_AUTH_HANDLER, EmpathyUoaAuthHandlerPriv);

  self->priv->manager = empathy_uoa_manager_dup ();
}

static void
empathy_uoa_auth_handler_dispose (GObject *object)
{
  EmpathyUoaAuthHandler *self = (EmpathyUoaAuthHandler *) object;

  tp_clear_object (&self->priv->manager);

  G_OBJECT_CLASS (empathy_uoa_auth_handler_parent_class)->dispose (object);
}

static void
empathy_uoa_auth_handler_class_init (EmpathyUoaAuthHandlerClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->dispose = empathy_uoa_auth_handler_dispose;

  g_type_class_add_private (klass, sizeof (EmpathyUoaAuthHandlerPriv));
}

EmpathyUoaAuthHandler *
empathy_uoa_auth_handler_new (void)
{
  return g_object_new (EMPATHY_TYPE_UOA_AUTH_HANDLER, NULL);
}

typedef struct
{
  TpChannel *channel;
  AgAuthData *auth_data;
  SignonAuthSession *session;
  SignonIdentity *identity;

  gchar *username;
} AuthContext;

static AuthContext *
auth_context_new (TpChannel *channel,
    AgAuthData *auth_data,
    SignonAuthSession *session,
    SignonIdentity *identity)
{
  AuthContext *ctx;

  ctx = g_slice_new0 (AuthContext);
  ctx->channel = g_object_ref (channel);
  ctx->auth_data = ag_auth_data_ref (auth_data);
  ctx->session = g_object_ref (session);
  ctx->identity = g_object_ref (identity);

  return ctx;
}

static void
auth_context_free (AuthContext *ctx)
{
  g_object_unref (ctx->channel);
  ag_auth_data_unref (ctx->auth_data);
  g_object_unref (ctx->session);
  g_object_unref (ctx->identity);
  g_free (ctx->username);
  g_slice_free (AuthContext, ctx);
}

static void
auth_context_done (AuthContext *ctx)
{
  tp_channel_close_async (ctx->channel, NULL, NULL);
  auth_context_free (ctx);
}

static void
auth_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  TpChannel *channel = (TpChannel *) source;
  AuthContext *ctx = user_data;
  GError *error = NULL;

  if (!empathy_sasl_auth_finish (channel, result, &error))
    {
      GHashTable *extra_params;

      DEBUG ("SASL Mechanism error: %s", error->message);
      g_clear_error (&error);

      /* Inform SSO that the access token didn't work and it should ask user
       * to re-grant access. */
      extra_params = tp_asv_new (
          SIGNON_SESSION_DATA_UI_POLICY, G_TYPE_INT,
              SIGNON_POLICY_REQUEST_PASSWORD,
          NULL);

      ag_auth_data_insert_parameters (ctx->auth_data, extra_params);

      signon_auth_session_process (ctx->session,
          ag_auth_data_get_parameters (ctx->auth_data),
          ag_auth_data_get_mechanism (ctx->auth_data),
          NULL, NULL);

      g_hash_table_unref (extra_params);
    }
  else
    {
      DEBUG ("Auth on %s suceeded", tp_proxy_get_object_path (channel));
    }

  auth_context_done (ctx);
}

static void
session_process_cb (SignonAuthSession *session,
    GHashTable *session_data,
    const GError *error,
    gpointer user_data)
{
  AuthContext *ctx = user_data;
  const gchar *access_token;
  const gchar *client_id;

  if (error != NULL)
    {
      DEBUG ("Error processing the session: %s", error->message);
      auth_context_done (ctx);
      return;
    }

  access_token = tp_asv_get_string (session_data, "AccessToken");
  client_id = tp_asv_get_string (ag_auth_data_get_parameters (ctx->auth_data),
      "ClientId");

  switch (empathy_sasl_channel_select_mechanism (ctx->channel))
    {
      case EMPATHY_SASL_MECHANISM_FACEBOOK:
        empathy_sasl_auth_facebook_async (ctx->channel,
            client_id, access_token,
            auth_cb, ctx);
        break;

      case EMPATHY_SASL_MECHANISM_WLM:
        empathy_sasl_auth_wlm_async (ctx->channel,
            access_token,
            auth_cb, ctx);
        break;

      case EMPATHY_SASL_MECHANISM_GOOGLE:
        empathy_sasl_auth_google_async (ctx->channel,
            ctx->username, access_token,
            auth_cb, ctx);
        break;

      default:
        g_assert_not_reached ();
    }
}

static void
identity_query_info_cb (SignonIdentity *identity,
    const SignonIdentityInfo *info,
    const GError *error,
    gpointer user_data)
{
  AuthContext *ctx = user_data;

  if (error != NULL)
    {
      DEBUG ("Error querying info from identity: %s", error->message);
      auth_context_done (ctx);
      return;
    }

  ctx->username = g_strdup (signon_identity_info_get_username (info));

  signon_auth_session_process (ctx->session,
      ag_auth_data_get_parameters (ctx->auth_data),
      ag_auth_data_get_mechanism (ctx->auth_data),
      session_process_cb,
      ctx);
}

void
empathy_uoa_auth_handler_start (EmpathyUoaAuthHandler *self,
    TpChannel *channel,
    TpAccount *tp_account)
{
  const GValue *id_value;
  AgAccountId id;
  AgAccount *account;
  GList *l = NULL;
  AgAccountService *service;
  AgAuthData *auth_data;
  guint cred_id;
  SignonIdentity *identity;
  SignonAuthSession *session;
  GError *error = NULL;

  g_return_if_fail (TP_IS_CHANNEL (channel));
  g_return_if_fail (TP_IS_ACCOUNT (tp_account));
  g_return_if_fail (empathy_uoa_auth_handler_supports (self, channel,
      tp_account));

  DEBUG ("Start UOA auth for account: %s",
      tp_proxy_get_object_path (tp_account));

  id_value = tp_account_get_storage_identifier (tp_account);
  id = g_value_get_uint (id_value);

  account = ag_manager_get_account (self->priv->manager, id);
  if (account != NULL)
    l = ag_account_list_services_by_type (account, EMPATHY_UOA_SERVICE_TYPE);
  if (l == NULL)
    {
      DEBUG ("Couldn't find IM service for AgAccountId %u", id);
      g_object_unref (account);
      tp_channel_close_async (channel, NULL, NULL);
      return;
    }

  /* Assume there is only one IM service */
  service = ag_account_service_new (account, l->data);
  ag_service_list_free (l);
  g_object_unref (account);

  auth_data = ag_account_service_get_auth_data (service);
  cred_id = ag_auth_data_get_credentials_id (auth_data);
  identity = signon_identity_new_from_db (cred_id);
  session = signon_identity_create_session (identity,
      ag_auth_data_get_method (auth_data),
      &error);
  if (session == NULL)
    {
      DEBUG ("Error creating a SignonAuthSession: %s", error->message);
      tp_channel_close_async (channel, NULL, NULL);
      goto cleanup;
    }

  /* Query UOA for more info */
  signon_identity_query_info (identity,
      identity_query_info_cb,
      auth_context_new (channel, auth_data, session, identity));

cleanup:
  ag_auth_data_unref (auth_data);
  g_object_unref (service);
  g_object_unref (identity);
  g_object_unref (session);
}

gboolean
empathy_uoa_auth_handler_supports (EmpathyUoaAuthHandler *self,
    TpChannel *channel,
    TpAccount *account)
{
  const gchar *provider;
  EmpathySaslMechanism mech;

  g_return_val_if_fail (TP_IS_CHANNEL (channel), FALSE);
  g_return_val_if_fail (TP_IS_ACCOUNT (account), FALSE);

  provider = tp_account_get_storage_provider (account);

  if (tp_strdiff (provider, EMPATHY_UOA_PROVIDER))
    return FALSE;

  mech = empathy_sasl_channel_select_mechanism (channel);
  return mech == EMPATHY_SASL_MECHANISM_FACEBOOK ||
      mech == EMPATHY_SASL_MECHANISM_WLM ||
      mech == EMPATHY_SASL_MECHANISM_GOOGLE;
}
