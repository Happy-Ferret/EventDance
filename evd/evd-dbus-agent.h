/*
 * evd-dbus-agent.h
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2009/2010, Igalia S.L.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 3, or (at your option) any later version as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License at http://www.gnu.org/licenses/lgpl-3.0.txt
 * for more details.
 */

#ifndef __EVD_DBUS_AGENT_H__
#define __EVD_DBUS_AGENT_H__

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef void (* EvdDBusAgentProxyPropertiesChangedCb) (GObject     *object,
                                                       guint        proxy_id,
                                                       GVariant    *changed_properties,
                                                       GStrv       *invalidated_properties,
                                                       gpointer     user_data);

typedef void (* EvdDBusAgentProxySignalCb)            (GObject     *object,
                                                       guint        proxy_id,
                                                       const gchar *signal_name,
                                                       GVariant    *parameters,
                                                       gpointer     user_data);

typedef struct
{
  EvdDBusAgentProxySignalCb proxy_signal;
  EvdDBusAgentProxyPropertiesChangedCb proxy_properties_changed;
} EvdDBusAgentVTable;

void              evd_dbus_agent_create_address_alias         (GObject     *object,
                                                               const gchar *address,
                                                               const gchar *alias);

void              evd_dbus_agent_new_connection               (GObject             *object,
                                                               const gchar         *addr,
                                                               gboolean             reuse,
                                                               GCancellable        *cancellable,
                                                               GAsyncReadyCallback  callback,
                                                               gpointer             user_data);
guint             evd_dbus_agent_new_connection_finish        (GObject       *object,
                                                               GAsyncResult  *result,
                                                               GError       **error);
gboolean          evd_dbus_agent_close_connection             (GObject  *object,
                                                               guint     connection_id,
                                                               GError  **error);
GDBusConnection * evd_dbus_agent_get_connection               (GObject  *obj,
                                                               guint     connection_id,
                                                               GError  **error);

void              evd_dbus_agent_new_proxy                    (GObject             *object,
                                                               guint                conn_id,
                                                               GDBusProxyFlags      flags,
                                                               const gchar         *name,
                                                               const gchar         *object_path,
                                                               const gchar         *iface_name,
                                                               GCancellable        *cancellable,
                                                               GAsyncReadyCallback  callback,
                                                               gpointer             user_data);
guint             evd_dbus_agent_new_proxy_finish             (GObject        *object,
                                                               GAsyncResult   *result,
                                                               GError        **error);
gboolean          evd_dbus_agent_close_proxy                  (GObject  *object,
                                                               guint     proxy_id,
                                                               GError  **error);
GDBusProxy *      evd_dbus_agent_get_proxy                    (GObject  *obj,
                                                               guint     proxy_id,
                                                               GError  **error);

gboolean          evd_dbus_agent_watch_proxy_signals          (GObject                    *object,
                                                               guint                       proxy_id,
                                                               EvdDBusAgentProxySignalCb  callback,
                                                               gpointer                    user_data,
                                                               GError                    **error);
gboolean          evd_dbus_agent_watch_proxy_property_changes (GObject                               *object,
                                                               guint                                  proxy_id,
                                                               EvdDBusAgentProxyPropertiesChangedCb   callback,
                                                               gpointer                               user_data,
                                                               GError                               **error);

guint             evd_dbus_agent_register_object              (GObject             *object,
                                                               guint                connection_id,
                                                               const gchar         *object_path,
                                                               GDBusInterfaceInfo  *interface_info,
                                                               GError             **error);
gboolean          evd_dbus_agent_unregister_object            (GObject  *object,
                                                               guint     connection_id,
                                                               guint     registration_id,
                                                               GError  **error);

void              evd_dbus_agent_set_object_vtable            (GObject             *object,
                                                               EvdDBusAgentVTable  *vtable,
                                                               gpointer             user_data);

G_END_DECLS

#endif /* __EVD_DBUS_AGENT_H__ */
