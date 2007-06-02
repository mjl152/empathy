/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2007 Collabora Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 */

#ifndef __EMPATHY_LOG_MANAGER_H__
#define __EMPATHY_LOG_MANAGER_H__

#include <glib-object.h>

#include <libmissioncontrol/mc-account.h>

#include "gossip-message.h"

G_BEGIN_DECLS

#define EMPATHY_TYPE_LOG_MANAGER         (empathy_log_manager_get_type ())
#define EMPATHY_LOG_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_LOG_MANAGER, EmpathyLogManager))
#define EMPATHY_LOG_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), EMPATHY_TYPE_LOG_MANAGER, EmpathyLogManagerClass))
#define EMPATHY_IS_LOG_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_LOG_MANAGER))
#define EMPATHY_IS_LOG_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_LOG_MANAGER))
#define EMPATHY_LOG_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_LOG_MANAGER, EmpathyLogManagerClass))

typedef struct _EmpathyLogManager      EmpathyLogManager;
typedef struct _EmpathyLogManagerClass EmpathyLogManagerClass;
typedef struct _EmpathyLogManagerPriv  EmpathyLogManagerPriv;

struct _EmpathyLogManager {
	GObject parent;
};

struct _EmpathyLogManagerClass {
	GObjectClass parent_class;
};

GType              empathy_log_manager_get_type              (void) G_GNUC_CONST;
EmpathyLogManager *empathy_log_manager_new                   (void);
void               empathy_log_manager_add_message           (EmpathyLogManager *manager,
							      const gchar       *chat_id,
							      GossipMessage     *message);
GList *            empathy_log_manager_get_dates             (EmpathyLogManager *manager,
							      McAccount         *account,
							      const gchar       *chat_id);
GList *            empathy_log_manager_get_messages_for_date (EmpathyLogManager *manager,
							      McAccount         *account,
							      const gchar       *chat_id,
							      const gchar       *date);
GList *            empathy_log_manager_get_last_messages     (EmpathyLogManager *manager,
							      McAccount         *account,
							      const gchar       *chat_id);

G_END_DECLS

#endif /* __EMPATHY_LOG_MANAGER_H__ */
