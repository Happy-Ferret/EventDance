/*
 * evd-dbus-bridge.c
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

#include <json-glib/json-glib.h>

#include "evd-dbus-bridge.h"

#include "evd-utils.h"
#include "evd-dbus-agent.h"

G_DEFINE_TYPE (EvdDBusBridge, evd_dbus_bridge, G_TYPE_OBJECT)

#define EVD_DBUS_BRIDGE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                          EVD_TYPE_DBUS_BRIDGE, \
                                          EvdDBusBridgePrivate))

enum EvdDBusBridgeCmd
{
  EVD_DBUS_BRIDGE_CMD_NONE,

  EVD_DBUS_BRIDGE_CMD_ERROR,
  EVD_DBUS_BRIDGE_CMD_REPLY,
  EVD_DBUS_BRIDGE_CMD_NEW_CONNECTION,
  EVD_DBUS_BRIDGE_CMD_CLOSE_CONNECTION,
  EVD_DBUS_BRIDGE_CMD_OWN_NAME,
  EVD_DBUS_BRIDGE_CMD_UNOWN_NAME,
  EVD_DBUS_BRIDGE_CMD_NAME_ACQUIRED,
  EVD_DBUS_BRIDGE_CMD_NAME_LOST,
  EVD_DBUS_BRIDGE_CMD_REGISTER_OBJECT,
  EVD_DBUS_BRIDGE_CMD_UNREGISTER_OBJECT,
  EVD_DBUS_BRIDGE_CMD_NEW_PROXY,
  EVD_DBUS_BRIDGE_CMD_CLOSE_PROXY,
  EVD_DBUS_BRIDGE_CMD_CALL_METHOD,
  EVD_DBUS_BRIDGE_CMD_CALL_METHOD_RETURN,
  EVD_DBUS_BRIDGE_CMD_EMIT_SIGNAL,

  EVD_DBUS_BRIDGE_CMD_PAD0,
  EVD_DBUS_BRIDGE_CMD_PAD1,
  EVD_DBUS_BRIDGE_CMD_PAD2,
  EVD_DBUS_BRIDGE_CMD_PAD3,
  EVD_DBUS_BRIDGE_CMD_PAD4,

  EVD_DBUS_BRIDGE_CMD_LAST
};

enum EvdDBusBridgeErr
{
  EVD_DBUS_BRIDGE_ERR_FAILED,
  EVD_DBUS_BRIDGE_ERR_INVALID_MSG,
  EVD_DBUS_BRIDGE_ERR_UNKNOW_COMMAND,
  EVD_DBUS_BRIDGE_ERR_INVALID_SUBJECT,
  EVD_DBUS_BRIDGE_ERR_INVALID_ARGS,
  EVD_DBUS_BRIDGE_ERR_CONNECTION_FAILED,
  EVD_DBUS_BRIDGE_ERR_ALREADY_REGISTERED,
  EVD_DBUS_BRIDGE_ERR_PROXY_FAILED,
  EVD_DBUS_BRIDGE_ERR_UNKNOWN_METHOD,

  EVD_DBUS_BRIDGE_ERR_PAD0,
  EVD_DBUS_BRIDGE_ERR_PAD1,
  EVD_DBUS_BRIDGE_ERR_PAD2,
  EVD_DBUS_BRIDGE_ERR_PAD3,
  EVD_DBUS_BRIDGE_ERR_PAD4,

  EVD_DBUS_BRIDGE_ERR_LAST
};

/* private data */
struct _EvdDBusBridgePrivate
{
  EvdDBusAgentVTable agent_vtable;

#ifdef ENABLE_TESTS
  EvdDBusBridgeSendMsgCb send_msg_callback;
  gpointer send_msg_user_data;
#endif
};

typedef struct
{
  EvdDBusBridge *bridge;
  GObject *obj;
  guint8 cmd;
  guint64 serial;
  gint32 subject;
  gchar *args;
  gint err_code;
} MsgClosure;

/* signals */
enum
{
  SIGNAL_LAST
};

//static guint evd_dbus_bridge_signals[SIGNAL_LAST] = { 0 };

/* properties */
enum
{
  PROP_0
};

static void     evd_dbus_bridge_class_init             (EvdDBusBridgeClass *class);
static void     evd_dbus_bridge_init                   (EvdDBusBridge *self);

static void     evd_dbus_bridge_finalize               (GObject *obj);
static void     evd_dbus_bridge_dispose                (GObject *obj);

static void     evd_dbus_bridge_set_property           (GObject      *obj,
                                                        guint         prop_id,
                                                        const GValue *value,
                                                        GParamSpec   *pspec);
static void     evd_dbus_bridge_get_property           (GObject    *obj,
                                                        guint       prop_id,
                                                        GValue     *value,
                                                        GParamSpec *pspec);

static void     evd_dbus_bridge_send                   (EvdDBusBridge *self,
                                                        GObject       *obj,
                                                        guint8         cmd,
                                                        guint64        serial,
                                                        guint32        subject,
                                                        const gchar   *args);

static void     evd_dbus_bridge_on_proxy_signal        (GObject     *obj,
                                                        guint        proxy_id,
                                                        const gchar *signal_name,
                                                        GVariant    *parameters,
                                                        gpointer     user_data);
static void     evd_dbus_bridge_on_proxy_props_changed (GObject     *obj,
                                                        guint        proxy_uuid,
                                                        GVariant    *changed_properties,
                                                        GStrv       *invalidated_properties,
                                                        gpointer     user_data);

static void     evd_dbus_bridge_on_reg_obj_call_method (GObject     *object,
                                                        const gchar *sender,
                                                        const gchar *method_name,
                                                        guint        registration_id,
                                                        GVariant    *parameters,
                                                        guint64      serial,
                                                        gpointer     user_data);

static void
evd_dbus_bridge_class_init (EvdDBusBridgeClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_dbus_bridge_dispose;
  obj_class->finalize = evd_dbus_bridge_finalize;
  obj_class->get_property = evd_dbus_bridge_get_property;
  obj_class->set_property = evd_dbus_bridge_set_property;

  g_type_class_add_private (obj_class, sizeof (EvdDBusBridgePrivate));
}

static void
evd_dbus_bridge_init (EvdDBusBridge *self)
{
  EvdDBusBridgePrivate *priv;

  priv = EVD_DBUS_BRIDGE_GET_PRIVATE (self);
  self->priv = priv;

  priv->agent_vtable.proxy_signal = evd_dbus_bridge_on_proxy_signal;
  priv->agent_vtable.proxy_properties_changed = evd_dbus_bridge_on_proxy_props_changed;
  priv->agent_vtable.method_call = evd_dbus_bridge_on_reg_obj_call_method;
}

static void
evd_dbus_bridge_dispose (GObject *obj)
{
  //  EvdDBusBridge *self = EVD_DBUS_BRIDGE (obj);

  G_OBJECT_CLASS (evd_dbus_bridge_parent_class)->dispose (obj);
}

static void
evd_dbus_bridge_finalize (GObject *obj)
{
  //  EvdDBusBridge *self = EVD_DBUS_BRIDGE (obj);

  G_OBJECT_CLASS (evd_dbus_bridge_parent_class)->finalize (obj);
}

static void
evd_dbus_bridge_set_property (GObject      *obj,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  EvdDBusBridge *self;

  self = EVD_DBUS_BRIDGE (obj);

  switch (prop_id)
    {

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_dbus_bridge_get_property (GObject    *obj,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  EvdDBusBridge *self;

  self = EVD_DBUS_BRIDGE (obj);

  switch (prop_id)
    {

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static MsgClosure *
evd_dbus_bridge_new_msg_closure (EvdDBusBridge *self,
                                 GObject       *obj,
                                 guint8         cmd,
                                 guint64        serial,
                                 gint32         subject,
                                 const gchar   *args,
                                 gint           err_code)
{
  MsgClosure *closure;

  closure = g_slice_new (MsgClosure);

  g_object_ref (self);
  closure->bridge = self;

  g_object_ref (obj);
  closure->obj = obj;

  closure->cmd = cmd;
  closure->serial = serial;
  closure->subject = subject;
  closure->args = g_strdup (args);
  closure->err_code = err_code;

  return closure;
}

static void
evd_dbus_bridge_free_msg_closure (MsgClosure *closure)
{
  g_object_unref (closure->bridge);
  g_object_unref (closure->obj);
  g_free (closure->args);

  g_slice_free (MsgClosure, closure);
}

static void
evd_dbus_bridge_on_proxy_signal (GObject     *obj,
                                 guint        proxy_id,
                                 const gchar *signal_name,
                                 GVariant    *parameters,
                                 gpointer     user_data)
{
  EvdDBusBridge *self = EVD_DBUS_BRIDGE (user_data);
  gchar *json;
  gchar *escaped_json;
  gchar *args;
  const gchar *signature;

  json = json_data_from_gvariant (parameters, NULL);
  escaped_json = g_strescape (json, "\b\f\n\r\t\'");
  signature = g_variant_get_type_string (parameters);

  args = g_strdup_printf ("'%s','%s','%s'",
                          signal_name,
                          escaped_json,
                          signature);

  evd_dbus_bridge_send (self,
                        obj,
                        EVD_DBUS_BRIDGE_CMD_EMIT_SIGNAL,
                        0,
                        proxy_id,
                        args);

  g_free (args);
  g_free (escaped_json);
  g_free (json);
}

static void
evd_dbus_bridge_on_proxy_props_changed (GObject     *obj,
                                        guint        proxy_id,
                                        GVariant    *changed_properties,
                                        GStrv       *invalidated_properties,
                                        gpointer     user_data)
{
  /* @TODO */
}

static void
evd_dbus_bridge_on_reg_obj_call_method (GObject     *obj,
                                        const gchar *sender,
                                        const gchar *method_name,
                                        guint        registration_id,
                                        GVariant    *parameters,
                                        guint64      serial,
                                        gpointer     user_data)
{
  EvdDBusBridge *self = EVD_DBUS_BRIDGE (user_data);
  gchar *json;
  gchar *escaped_json;
  gchar *args;
  const gchar *signature;

  json = json_data_from_gvariant (parameters, NULL);
  escaped_json = g_strescape (json, "\b\f\n\r\t\'");
  signature = g_variant_get_type_string (parameters);

  args = g_strdup_printf ("'%s','%s','%s',0,0",
                          method_name,
                          escaped_json,
                          signature);

  evd_dbus_bridge_send (self,
                        obj,
                        EVD_DBUS_BRIDGE_CMD_CALL_METHOD,
                        serial,
                        registration_id,
                        args);

  g_free (args);
  g_free (escaped_json);
  g_free (json);
}

static void
evd_dbus_bridge_send (EvdDBusBridge *self,
                      GObject       *obj,
                      guint8         cmd,
                      guint64        serial,
                      guint32        subject,
                      const gchar   *args)
{
  gchar *json;

  json = g_strdup_printf ("[%u,%lu,%u,\"[%s]\"]", cmd, serial, subject, args);

#ifdef ENABLE_TESTS
  if (self->priv->send_msg_callback != NULL)
    {
      self->priv->send_msg_callback (self,
                                     obj,
                                     json,
                                     self->priv->send_msg_user_data);
    }
#endif

  g_free (json);
}

static gboolean
evd_dbus_bridge_on_idle_send (gpointer user_data)
{
  MsgClosure *closure = (MsgClosure *) user_data;

  evd_dbus_bridge_send (closure->bridge,
                        closure->obj,
                        closure->cmd,
                        closure->serial,
                        closure->subject,
                        closure->args);

  evd_dbus_bridge_free_msg_closure (closure);

  return FALSE;
}

static void
evd_dbus_bridge_send_in_idle (EvdDBusBridge *self,
                              GObject       *obj,
                              guint8         cmd,
                              guint64        serial,
                              guint32        subject,
                              const gchar   *args)
{
  MsgClosure *closure;

  closure = evd_dbus_bridge_new_msg_closure (self,
                                             obj,
                                             cmd,
                                             serial,
                                             subject,
                                             args,
                                             0);

  evd_timeout_add (NULL,
                   0,
                   G_PRIORITY_DEFAULT,
                   evd_dbus_bridge_on_idle_send, closure);
}

static void
evd_dbus_bridge_send_error (EvdDBusBridge *self,
                            GObject       *obj,
                            guint64        serial,
                            guint32        subject,
                            gint           err_code,
                            const gchar   *err_msg)
{
  gchar *args;

  if (err_msg != NULL)
    args = g_strdup_printf ("%d,\\\"%s\\\"", err_code, err_msg);
  else
    args = g_strdup_printf ("%d", err_code);

  evd_dbus_bridge_send (self,
                        obj,
                        EVD_DBUS_BRIDGE_CMD_ERROR,
                        serial,
                        subject,
                        args);
  g_free (args);
}

static gboolean
evd_dbus_bridge_on_send_error_idle (gpointer user_data)
{
  MsgClosure *closure = (MsgClosure *) user_data;

  evd_dbus_bridge_send_error (closure->bridge,
                              closure->obj,
                              closure->serial,
                              closure->subject,
                              closure->err_code,
                              closure->args);

  evd_dbus_bridge_free_msg_closure (closure);

  return FALSE;
}

static void
evd_dbus_bridge_send_error_in_idle (EvdDBusBridge *self,
                                    GObject       *obj,
                                    guint64        serial,
                                    guint32        subject,
                                    gint           err_code,
                                    const gchar   *err_msg)
{
  MsgClosure *closure;

  closure = evd_dbus_bridge_new_msg_closure (self,
                                             obj,
                                             EVD_DBUS_BRIDGE_CMD_ERROR,
                                             serial,
                                             subject,
                                             err_msg,
                                             err_code);

  evd_timeout_add (NULL,
                   0,
                   G_PRIORITY_DEFAULT,
                   evd_dbus_bridge_on_send_error_idle,
                   closure);
}

static void
evd_dbus_bridge_on_new_connection (GObject      *obj,
                                   GAsyncResult *res,
                                   gpointer      user_data)
{
  EvdDBusBridge *self;
  MsgClosure *closure = (MsgClosure *) user_data;
  guint conn_id;
  GError *error = NULL;

  self = closure->bridge;

  if ( (conn_id = evd_dbus_agent_new_connection_finish (obj,
                                                        res,
                                                        &error)) == 0)
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  closure->serial,
                                  closure->subject,
                                  EVD_DBUS_BRIDGE_ERR_CONNECTION_FAILED,
                                  error->message);
      g_error_free (error);
    }
  else
    {
      gchar *args;

      args = g_strdup_printf ("%u", (guint32) conn_id);
      evd_dbus_bridge_send (closure->bridge,
                            closure->obj,
                            EVD_DBUS_BRIDGE_CMD_REPLY,
                            closure->serial,
                            closure->subject,
                            args);
      g_free (args);
    }

  evd_dbus_bridge_free_msg_closure (closure);
}

static void
evd_dbus_bridge_new_connection (EvdDBusBridge *self,
                                GObject       *obj,
                                guint64        serial,
                                const gchar   *args)
{
  gchar *addr;
  MsgClosure *closure;
  GVariant *variant_args;

  variant_args = json_data_to_gvariant (args, -1, "(s)", NULL);
  if (variant_args == NULL)
    {
      evd_dbus_bridge_send_error_in_idle (self,
                                          obj,
                                          serial,
                                          0,
                                          EVD_DBUS_BRIDGE_ERR_INVALID_ARGS,
                                          NULL);
      return;
    }

  g_variant_get (variant_args, "(s)", &addr);

  closure = evd_dbus_bridge_new_msg_closure (self,
                                             obj,
                                             EVD_DBUS_BRIDGE_CMD_NEW_CONNECTION,
                                             serial,
                                             0,
                                             NULL,
                                             0);
  evd_dbus_agent_new_connection (obj,
                                 addr,
                                 FALSE,
                                 NULL,
                                 evd_dbus_bridge_on_new_connection,
                                 closure);

  g_free (addr);
  g_variant_unref (variant_args);
}

static void
evd_dbus_bridge_close_connection (EvdDBusBridge *self,
                                  GObject       *obj,
                                  guint64        serial,
                                  guint32        subject)
{
  GError *error = NULL;

  if (evd_dbus_agent_close_connection (obj, subject, &error))
    {
      evd_dbus_bridge_send_in_idle (self,
                                    obj,
                                    EVD_DBUS_BRIDGE_CMD_REPLY,
                                    serial,
                                    subject,
                                    "");
    }
  else
    {
      evd_dbus_bridge_send_error_in_idle (self,
                                          obj,
                                          serial,
                                          subject,
                                          EVD_DBUS_BRIDGE_ERR_INVALID_SUBJECT,
                                          error->message);
      g_error_free (error);
    }
}

static void
evd_dbus_bridge_name_acquired (GDBusConnection *connection,
                               const gchar     *name,
                               gpointer         user_data)
{
  MsgClosure *closure = (MsgClosure *) user_data;

  evd_dbus_bridge_send (closure->bridge,
                        closure->obj,
                        EVD_DBUS_BRIDGE_CMD_NAME_ACQUIRED,
                        0,
                        closure->subject,
                        closure->args);
}

static void
evd_dbus_bridge_name_lost (GDBusConnection *connection,
                           const gchar     *name,
                           gpointer         user_data)
{
  MsgClosure *closure = (MsgClosure *) user_data;

  evd_dbus_bridge_send (closure->bridge,
                        closure->obj,
                        EVD_DBUS_BRIDGE_CMD_NAME_LOST,
                        0,
                        closure->subject,
                        closure->args);
}

static void
evd_dbus_bridge_own_name (EvdDBusBridge *self,
                          GObject       *obj,
                          guint64        serial,
                          guint32        subject,
                          const gchar   *args)
{
  GVariant *variant_args;
  gchar *name;
  guint32 flags;
  GDBusConnection *dbus_conn;
  GError *error = NULL;
  guint owning_id;
  MsgClosure *closure;
  gchar *st_args;

  variant_args = json_data_to_gvariant (args, -1, "(su)", NULL);
  if (variant_args == NULL)
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  0,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_ARGS,
                                  NULL);
      return;
    }

  g_variant_get (variant_args, "(su)", &name, &flags);

  dbus_conn = evd_dbus_agent_get_connection (obj, subject, &error);
  if (dbus_conn == NULL)
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  0,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_SUBJECT,
                                  NULL);

      g_error_free (error);

      goto free;
    }

  closure = evd_dbus_bridge_new_msg_closure (self,
                                             obj,
                                             EVD_DBUS_BRIDGE_CMD_OWN_NAME,
                                             serial,
                                             subject,
                                             NULL,
                                             0);

  owning_id =
    g_bus_own_name_on_connection (dbus_conn,
                             name,
                             flags,
                             evd_dbus_bridge_name_acquired,
                             evd_dbus_bridge_name_lost,
                             closure,
                             (GDestroyNotify) evd_dbus_bridge_free_msg_closure);

  st_args = g_strdup_printf ("%u", owning_id);
  closure->args = g_strdup (st_args);
  evd_dbus_bridge_send (self,
                        obj,
                        EVD_DBUS_BRIDGE_CMD_REPLY,
                        serial,
                        subject,
                        st_args);
  g_free (st_args);

 free:
  g_free (name);
  g_variant_unref (variant_args);
}

static void
evd_dbus_bridge_unown_name (EvdDBusBridge *self,
                            GObject       *obj,
                            guint64        serial,
                            guint32        subject,
                            const gchar   *args)
{
  GVariant *variant_args;
  guint owning_id;

  variant_args = json_data_to_gvariant (args, -1, "(u)", NULL);
  if (variant_args == NULL)
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  0,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_ARGS,
                                  NULL);
      return;
    }

  g_variant_get (variant_args, "(u)", &owning_id);

  g_bus_unown_name (owning_id);

  evd_dbus_bridge_send (self,
                        obj,
                        EVD_DBUS_BRIDGE_CMD_REPLY,
                        serial,
                        subject,
                        "");

  g_variant_unref (variant_args);
}

static void
evd_dbus_bridge_register_object (EvdDBusBridge *self,
                                 GObject       *obj,
                                 guint64        serial,
                                 guint32        subject,
                                 const gchar   *args)
{
  GVariant *variant_args;
  gchar *object_path;
  gchar *iface_data;
  gchar *node_data;
  guint reg_id = 0;
  GDBusNodeInfo *node_info = NULL;
  GError *error = NULL;

  variant_args = json_data_to_gvariant (args, -1, "(ss)", NULL);
  if (variant_args == NULL)
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  0,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_ARGS,
                                  NULL);
      return;
    }

  g_variant_get (variant_args, "(ss)", &object_path, &iface_data);

  /* create interface info */
  node_data = g_strdup_printf ("<node>%s</node>", iface_data);
  node_info = g_dbus_node_info_new_for_xml (node_data, &error);
  if (node_info != NULL && node_info->interfaces != NULL)
    {
      GDBusInterfaceInfo *iface_info;

      iface_info = node_info->interfaces[0];
      reg_id = evd_dbus_agent_register_object (obj,
                                               (gint) subject,
                                               object_path,
                                               iface_info,
                                               &error);

      if (reg_id > 0)
        {
          gchar *args;

          args = g_strdup_printf ("%u", reg_id);
          evd_dbus_bridge_send (self,
                                obj,
                                EVD_DBUS_BRIDGE_CMD_REPLY,
                                serial,
                                subject,
                                args);
          g_free (args);
        }
      else
        {
          evd_dbus_bridge_send_error (self,
                                      obj,
                                      serial,
                                      subject,
                                      EVD_DBUS_BRIDGE_ERR_ALREADY_REGISTERED,
                                      NULL);
          g_error_free (error);
        }
    }
  else
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  subject,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_ARGS,
                                  NULL);
    }

  if (node_info != NULL)
    g_dbus_node_info_unref (node_info);
  g_free (node_data);
  g_free (iface_data);
  g_free (object_path);
  g_variant_unref (variant_args);
}

static void
evd_dbus_bridge_unregister_object (EvdDBusBridge *self,
                                   GObject       *obj,
                                   guint64        serial,
                                   guint32        subject,
                                   const gchar   *args)
{
  GError *error = NULL;

  if (evd_dbus_agent_unregister_object (obj, subject, &error))
    {
      evd_dbus_bridge_send (self, obj, EVD_DBUS_BRIDGE_CMD_REPLY, serial, subject, "");
    }
  else
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  subject,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_SUBJECT,
                                  NULL);
      g_error_free (error);
    }
}

static void
evd_dbus_bridge_on_new_proxy (GObject      *obj,
                              GAsyncResult *res,
                              gpointer      user_data)
{
  MsgClosure *closure = (MsgClosure *) user_data;
  guint proxy_id;
  GError *error = NULL;

  if ( (proxy_id = evd_dbus_agent_new_proxy_finish (obj, res, &error)) > 0)
    {
      gchar *args;

      args = g_strdup_printf ("%u", proxy_id);
      evd_dbus_bridge_send (closure->bridge,
                            closure->obj,
                            EVD_DBUS_BRIDGE_CMD_REPLY,
                            closure->serial,
                            closure->subject,
                            args);
      g_free (args);
    }
  else
    {
      evd_dbus_bridge_send_error (closure->bridge,
                                  closure->obj,
                                  closure->serial,
                                  closure->subject,
                                  EVD_DBUS_BRIDGE_ERR_PROXY_FAILED,
                                  error->message);
      g_error_free (error);
    }

  evd_dbus_bridge_free_msg_closure (closure);
}

static void
evd_dbus_bridge_new_proxy (EvdDBusBridge *self,
                           GObject       *obj,
                           guint64        serial,
                           guint32        subject,
                           const gchar   *args)
{
  GVariant *variant_args;
  guint flags;
  gchar *name;
  gchar *obj_path;
  gchar *iface_name;
  MsgClosure *closure;

  variant_args = json_data_to_gvariant (args, -1, "(sssu)", NULL);
  if (variant_args == NULL)
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  0,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_ARGS,
                                  NULL);
      return;
    }

  g_variant_get (variant_args, "(sssu)",
                 &name,
                 &obj_path,
                 &iface_name,
                 &flags);

  closure = evd_dbus_bridge_new_msg_closure (self,
                                             obj,
                                             EVD_DBUS_BRIDGE_CMD_NEW_PROXY,
                                             serial,
                                             subject,
                                             NULL,
                                             0);

  evd_dbus_agent_new_proxy (obj,
                            subject,
                            flags,
                            name,
                            obj_path,
                            iface_name,
                            NULL,
                            evd_dbus_bridge_on_new_proxy,
                            closure);

  g_free (iface_name);
  g_free (obj_path);
  g_free (name);
  g_variant_unref (variant_args);
}

static void
evd_dbus_bridge_close_proxy (EvdDBusBridge *self,
                             GObject       *obj,
                             guint64        serial,
                             guint32        subject,
                             const gchar   *args)
{
  GError *error = NULL;

  if (evd_dbus_agent_close_proxy (obj, subject, &error))
    {
      evd_dbus_bridge_send (self,
                            obj,
                            EVD_DBUS_BRIDGE_CMD_REPLY,
                            serial,
                            subject,
                            "");
    }
  else
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  subject,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_SUBJECT,
                                  NULL);
      g_error_free (error);
    }
}

static void
evd_dbus_proxy_on_call_method_return (GObject      *obj,
                                      GAsyncResult *res,
                                      gpointer      user_data)
{
  MsgClosure *closure = (MsgClosure *) user_data;
  GVariant *ret_variant;
  GError *error = NULL;

  ret_variant = g_dbus_proxy_call_finish (G_DBUS_PROXY (obj), res, &error);
  if (ret_variant != NULL)
    {
      gchar *json;
      gchar *escaped_json;
      const gchar *signature;
      gchar *args;

      json = json_data_from_gvariant (ret_variant, NULL);
      escaped_json = g_strescape (json, "\b\f\n\r\t\'");
      signature = g_variant_get_type_string (ret_variant);
      args = g_strdup_printf ("'%s','%s'", escaped_json, signature);

      evd_dbus_bridge_send (closure->bridge,
                            closure->obj,
                            EVD_DBUS_BRIDGE_CMD_CALL_METHOD_RETURN,
                            closure->serial,
                            closure->subject,
                            args);

      g_free (args);
      g_free (escaped_json);
      g_free (json);
      g_variant_unref (ret_variant);
    }
  else
    {
      gint err_code;
      gchar *err_msg = NULL;

      /* @TODO: organize this in a method to convert from
         DBus error to bridge error */
      if (error->code == G_DBUS_ERROR_INVALID_ARGS)
        err_code = EVD_DBUS_BRIDGE_ERR_INVALID_ARGS;
      else if (error->code == G_DBUS_ERROR_UNKNOWN_METHOD)
        err_code = EVD_DBUS_BRIDGE_ERR_UNKNOWN_METHOD;
      else
        {
          err_code = EVD_DBUS_BRIDGE_ERR_FAILED;
          err_msg = error->message;
        }

      evd_dbus_bridge_send_error (closure->bridge,
                                  closure->obj,
                                  closure->serial,
                                  closure->subject,
                                  err_code,
                                  err_msg);
      g_error_free (error);
    }

  evd_dbus_bridge_free_msg_closure (closure);
}

static void
evd_dbus_bridge_call_method (EvdDBusBridge *self,
                             GObject       *obj,
                             guint64        serial,
                             guint32        subject,
                             const gchar   *args)
{
  GVariant *variant_args;
  gchar *method_name;
  gchar *method_args;
  guint call_flags;
  gint timeout;
  gchar *signature;
  GDBusProxy *proxy;
  MsgClosure *closure;
  GVariant *params;

  variant_args = json_data_to_gvariant (args, -1, "(ssgui)", NULL);
  if (variant_args == NULL)
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  0,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_ARGS,
                                  NULL);
      return;
    }

  g_variant_get (variant_args, "(ssgui)",
                 &method_name,
                 &method_args,
                 &signature,
                 &call_flags,
                 &timeout);

  params = json_data_to_gvariant (method_args, -1, signature, NULL);
  if (params == NULL)
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  0,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_ARGS,
                                  NULL);
      goto out;
    }

  proxy = evd_dbus_agent_get_proxy (obj, subject, NULL);
  if (proxy == NULL)
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  0,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_SUBJECT,
                                  NULL);
      goto out;
    }

  closure = evd_dbus_bridge_new_msg_closure (self,
                                             obj,
                                             EVD_DBUS_BRIDGE_CMD_CALL_METHOD,
                                             serial,
                                             subject,
                                             args,
                                             0);

  g_dbus_proxy_call (proxy,
                     method_name,
                     params,
                     call_flags,
                     timeout,
                     NULL,
                     evd_dbus_proxy_on_call_method_return,
                     closure);

 out:
  g_free (signature);
  g_free (method_args);
  g_free (method_name);
  g_variant_unref (variant_args);
}

static void
evd_dbus_bridge_call_method_return (EvdDBusBridge *self,
                                    GObject       *obj,
                                    guint64        serial,
                                    guint32        subject,
                                    const gchar   *args)
{
  GVariant *variant_args;
  gchar *return_args;
  gchar *signature;
  GVariant *return_variant;

  variant_args = json_data_to_gvariant (args, -1, "(ss)", NULL);
  if (variant_args == NULL)
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  0,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_ARGS,
                                  NULL);
      return;
    }

  g_variant_get (variant_args, "(ss)", &return_args, &signature);

  return_variant = json_data_to_gvariant (return_args, -1, signature, NULL);
  if (return_variant == NULL)
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  0,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_ARGS,
                                  NULL);
      goto out;
    }

  if (! evd_dbus_agent_method_call_return (obj,
                                           subject,
                                           serial,
                                           return_variant,
                                           NULL))
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  0,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_SUBJECT,
                                  NULL);
      goto out;
    }

 out:
  g_free (signature);
  g_free (return_args);
  g_variant_unref (variant_args);
}

static void
evd_dbus_bridge_emit_signal (EvdDBusBridge *self,
                             GObject       *obj,
                             guint64        serial,
                             guint32        subject,
                             const gchar   *args)
{
  GVariant *variant_args;
  gchar *signal_name;
  gchar *signal_args;
  gchar *signature;
  GVariant *signal_args_variant;
  GError *error = NULL;

  variant_args = json_data_to_gvariant (args, -1, "(sss)", NULL);
  if (variant_args == NULL)
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  0,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_ARGS,
                                  NULL);
      return;
    }

  g_variant_get (variant_args, "(sss)",
                 &signal_name,
                 &signal_args,
                 &signature);

  signal_args_variant = json_data_to_gvariant (signal_args,
                                               -1,
                                               signature,
                                               NULL);
  if (signal_args_variant == NULL)
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  0,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_ARGS,
                                  NULL);
      goto out;
    }

  if (! evd_dbus_agent_emit_signal (obj,
                                    subject,
                                    signal_name,
                                    signal_args_variant,
                                    &error))
    {
      gint err_code;
      gchar *err_msg = NULL;

      if (error->code == G_DBUS_ERROR_INVALID_ARGS)
        err_code = EVD_DBUS_BRIDGE_ERR_INVALID_ARGS;
      else
        {
          err_code = EVD_DBUS_BRIDGE_ERR_FAILED;
          err_msg = error->message;
        }

      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  subject,
                                  err_code,
                                  err_msg);
      g_error_free (error);
    }

 out:
  g_variant_unref (signal_args_variant);
  g_free (signature);
  g_free (signal_args);
  g_free (signal_name);
  g_variant_unref (variant_args);
}

/* public methods */

EvdDBusBridge *
evd_dbus_bridge_new (void)
{
  EvdDBusBridge *self;

  self = g_object_new (EVD_TYPE_DBUS_BRIDGE, NULL);

  return self;
}

#ifdef ENABLE_TESTS

void
evd_dbus_bridge_process_msg (EvdDBusBridge *self,
                             GObject       *object,
                             const gchar   *msg,
                             gsize          length)
{
  GVariant *variant_msg;
  guint8 cmd;
  guint64 serial;
  guint32 subject;
  gchar *args;

  evd_dbus_agent_set_object_vtable (object, &self->priv->agent_vtable, self);

  variant_msg = json_data_to_gvariant (msg, length, "(ytus)", NULL);
  if (variant_msg == NULL)
    {
      evd_dbus_bridge_send_error_in_idle (self,
                                          object,
                                          0,
                                          0,
                                          EVD_DBUS_BRIDGE_ERR_INVALID_MSG,
                                          NULL);
      return;
    }

  g_variant_get (variant_msg, "(ytus)", &cmd, &serial, &subject, &args);

  //  g_debug ("cmd: %u, serial: %lu, subject: %lu, args: %s", cmd, serial, subject, args);

  switch (cmd)
    {
    case EVD_DBUS_BRIDGE_CMD_NEW_CONNECTION:
      evd_dbus_bridge_new_connection (self, object, serial, args);
      break;

    case EVD_DBUS_BRIDGE_CMD_CLOSE_CONNECTION:
      evd_dbus_bridge_close_connection (self, object, serial, subject);
      break;

    case EVD_DBUS_BRIDGE_CMD_OWN_NAME:
      evd_dbus_bridge_own_name (self, object, serial, subject, args);
      break;

    case EVD_DBUS_BRIDGE_CMD_UNOWN_NAME:
      evd_dbus_bridge_unown_name (self, object, serial, subject, args);
      break;

    case EVD_DBUS_BRIDGE_CMD_REGISTER_OBJECT:
      evd_dbus_bridge_register_object (self, object, serial, subject, args);
      break;

    case EVD_DBUS_BRIDGE_CMD_UNREGISTER_OBJECT:
      evd_dbus_bridge_unregister_object (self, object, serial, subject, args);
      break;

    case EVD_DBUS_BRIDGE_CMD_NEW_PROXY:
      evd_dbus_bridge_new_proxy (self, object, serial, subject, args);
      break;

    case EVD_DBUS_BRIDGE_CMD_CLOSE_PROXY:
      evd_dbus_bridge_close_proxy (self, object, serial, subject, args);
      break;

    case EVD_DBUS_BRIDGE_CMD_CALL_METHOD:
      evd_dbus_bridge_call_method (self, object, serial, subject, args);
      break;

    case EVD_DBUS_BRIDGE_CMD_CALL_METHOD_RETURN:
      evd_dbus_bridge_call_method_return (self, object, serial, subject, args);
      break;

    case EVD_DBUS_BRIDGE_CMD_EMIT_SIGNAL:
      evd_dbus_bridge_emit_signal (self, object, serial, subject, args);
      break;

    default:
      evd_dbus_bridge_send_error_in_idle (self,
                                          object,
                                          serial,
                                          0,
                                          EVD_DBUS_BRIDGE_ERR_UNKNOW_COMMAND,
                                          NULL);
      break;
    }

  g_free (args);
  g_variant_unref (variant_msg);
}

void
evd_dbus_bridge_set_send_msg_callback (EvdDBusBridge          *self,
                                       EvdDBusBridgeSendMsgCb  callback,
                                       gpointer                user_data)
{
  g_return_if_fail (EVD_IS_DBUS_BRIDGE (self));

  self->priv->send_msg_callback = callback;
  self->priv->send_msg_user_data = user_data;
}

#endif /* ENABLE_TESTS */