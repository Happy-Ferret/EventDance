/*
 * evd-transport.c
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

#include <string.h>

#include "evd-transport.h"

#include "evd-utils.h"
#include "evd-marshal.h"
#include "evd-peer-manager.h"

#define PEER_MSG_KEY     "org.eventdance.lib.transport.PEER_MESSAGE"
#define PEER_CLOSING_KEY "org.eventdance.lib.Transport.PEER_CLOSING"

/* signals */
enum
{
  SIGNAL_RECEIVE,
  SIGNAL_NEW_PEER,
  SIGNAL_PEER_CLOSED,
  SIGNAL_LAST
};

typedef struct
{
  const gchar *buffer;
  gchar *text_buffer;
  gsize size;
} EvdTransportPeerMessage;

static guint evd_transport_signals[SIGNAL_LAST] = { 0 };

static void     evd_transport_receive_internal         (EvdTransport *self,
                                                        EvdPeer      *peer,
                                                        const gchar  *buffer,
                                                        gsize         size);
static void     evd_transport_notify_receive           (EvdTransport *self,
                                                        EvdPeer      *peer);

static void     evd_transport_notify_new_peer          (EvdTransport *self,
                                                        EvdPeer      *peer);
static EvdPeer *evd_transport_create_new_peer_internal (EvdTransport *self);

static void     evd_transport_notify_peer_closed       (EvdTransport *self,
                                                        EvdPeer      *peer,
                                                        gboolean      gracefully);

static void
evd_transport_base_init (gpointer g_class)
{
  static gboolean is_initialized = FALSE;

  EvdTransportInterface *iface = (EvdTransportInterface *) g_class;

  iface->receive = evd_transport_receive_internal;
  iface->notify_receive = evd_transport_notify_receive;
  iface->create_new_peer = evd_transport_create_new_peer_internal;
  iface->notify_new_peer = evd_transport_notify_new_peer;
  iface->notify_peer_closed = evd_transport_notify_peer_closed;

  if (! is_initialized)
    {
      evd_transport_signals[SIGNAL_RECEIVE] =
        g_signal_new ("receive",
                      G_TYPE_FROM_CLASS (g_class),
                      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                      G_STRUCT_OFFSET (EvdTransportInterface, signal_receive),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1,
                      EVD_TYPE_PEER);

      evd_transport_signals[SIGNAL_NEW_PEER] =
        g_signal_new ("new-peer",
                      G_TYPE_FROM_CLASS (g_class),
                      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                      G_STRUCT_OFFSET (EvdTransportInterface, signal_new_peer),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1,
                      EVD_TYPE_PEER);

      evd_transport_signals[SIGNAL_PEER_CLOSED] =
        g_signal_new ("peer-closed",
                      G_TYPE_FROM_CLASS (g_class),
                      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                      G_STRUCT_OFFSET (EvdTransportInterface, signal_peer_closed),
                      NULL, NULL,
                      evd_marshal_VOID__OBJECT_BOOLEAN,
                      G_TYPE_NONE, 2,
                      EVD_TYPE_PEER,
                      G_TYPE_BOOLEAN);

      is_initialized = TRUE;
    }

  iface->peer_manager = evd_peer_manager_get_default ();
}

static void
evd_transport_base_finalize (gpointer g_class)
{
  EvdTransportInterface *iface = (EvdTransportInterface *) g_class;

  g_object_unref (iface->peer_manager);
}

GType
evd_transport_get_type (void)
{
  static GType iface_type = 0;

  if (iface_type == 0)
    {
      static const GTypeInfo info = {
        sizeof (EvdTransportInterface),
        evd_transport_base_init,
        evd_transport_base_finalize,
        NULL,
      };

      iface_type = g_type_register_static (G_TYPE_INTERFACE, "EvdTransport",
                                           &info, 0);
    }

  return iface_type;
}

static void
evd_transport_free_peer_msg (gpointer  data,
                             GObject  *where_the_object_was)
{
  EvdTransportPeerMessage *msg;

  msg = g_object_get_data (G_OBJECT (where_the_object_was), PEER_MSG_KEY);

  if (msg != NULL)
    {
      g_slice_free (EvdTransportPeerMessage, msg);

      g_object_set_data (G_OBJECT (where_the_object_was), PEER_MSG_KEY, NULL);
    }
}

static void
evd_transport_notify_receive (EvdTransport *self, EvdPeer *peer)
{
  g_signal_emit (self, evd_transport_signals[SIGNAL_RECEIVE], 0, peer, NULL);
}

static void
evd_transport_receive_internal (EvdTransport *self,
                                EvdPeer      *peer,
                                const gchar  *buffer,
                                gsize         size)
{
  EvdTransportPeerMessage *msg;

  msg = g_object_get_data (G_OBJECT (peer), PEER_MSG_KEY);
  if (msg == NULL)
    {
      msg = g_slice_new (EvdTransportPeerMessage);
      g_object_set_data (G_OBJECT (peer), PEER_MSG_KEY, msg);

      g_object_weak_ref (G_OBJECT (peer), evd_transport_free_peer_msg, NULL);
    }

  msg->buffer = buffer;
  msg->size = size;
  msg->text_buffer = NULL;

  evd_transport_notify_receive (self, peer);

  if (msg->text_buffer != NULL)
    {
      g_slice_free1 (msg->size + 1, msg->text_buffer);
      msg->text_buffer = NULL;
    }
  msg->buffer = NULL;
  msg->size = 0;
}

static void
evd_transport_notify_new_peer (EvdTransport *self,
                               EvdPeer      *peer)
{
  g_signal_emit (self, evd_transport_signals[SIGNAL_NEW_PEER], 0, peer, NULL);
}

static gboolean
evd_transport_notify_new_peer_cb (gpointer user_data)
{
  EvdPeer *peer =  EVD_PEER (user_data);
  EvdTransport *self;

  /* @TODO: check if peer is still open */

  g_object_get (peer, "transport", &self, NULL);

  evd_transport_notify_new_peer (self, peer);

  g_object_unref (self);

  g_object_unref (peer);
  g_object_unref (self);

  return FALSE;
}

static EvdPeer *
evd_transport_create_new_peer_internal (EvdTransport *self)
{
  EvdPeerManager *peer_manager;
  EvdPeer *peer;

  peer_manager = EVD_TRANSPORT_GET_INTERFACE (self)->peer_manager;

  peer = g_object_new (EVD_TYPE_PEER,
                       "transport", self,
                       NULL);

  evd_peer_manager_add_peer (peer_manager, peer);

  g_object_ref (peer);
  g_object_ref (self);
  evd_timeout_add (g_main_context_get_thread_default (),
                   0,
                   G_PRIORITY_DEFAULT,
                   evd_transport_notify_new_peer_cb,
                   peer);

  return peer;
}

static void
evd_transport_notify_peer_closed (EvdTransport *self,
                                  EvdPeer      *peer,
                                  gboolean      gracefully)
{
  g_signal_emit (self,
                 evd_transport_signals[SIGNAL_PEER_CLOSED],
                 0,
                 peer,
                 gracefully,
                 NULL);
}

/* public methods */

gboolean
evd_transport_send (EvdTransport  *self,
                    EvdPeer       *peer,
                    const gchar   *buffer,
                    gsize          size,
                    GError       **error)
{
  g_return_val_if_fail (EVD_IS_TRANSPORT (self), FALSE);
  g_return_val_if_fail (EVD_IS_PEER (peer), FALSE);

  if (! EVD_TRANSPORT_GET_INTERFACE (self)->send (self,
                                                  peer,
                                                  buffer,
                                                  size,
                                                  NULL)
      && ! evd_peer_backlog_push_frame (peer, buffer, size, error))
    {
      return FALSE;
    }
  else
    {
      return TRUE;
    }
}

gboolean
evd_transport_send_text (EvdTransport  *self,
                         EvdPeer       *peer,
                         const gchar   *text,
                         GError       **error)
{
  gsize size;

  g_return_val_if_fail (EVD_IS_TRANSPORT (self), FALSE);
  g_return_val_if_fail (EVD_IS_PEER (peer), FALSE);

  size = strlen (text);

  return evd_transport_send (self, peer, text, size, error);
}

/**
 * evd_transport_receive:
 * @size: (out):
 *
 * Returns: (transfer none):
 **/
const gchar *
evd_transport_receive (EvdTransport *self,
                       EvdPeer       *peer,
                       gsize         *size)
{
  EvdTransportPeerMessage *msg;

  g_return_val_if_fail (EVD_IS_TRANSPORT (self), NULL);

  msg = g_object_get_data (G_OBJECT (peer), PEER_MSG_KEY);
  if (msg == NULL)
    return NULL;

  if (size != NULL)
    *size = msg->size;

  return msg->buffer;
}

/**
 * evd_transport_receive_text:
 *
 * Returns: (transfer none):
 **/
const gchar *
evd_transport_receive_text (EvdTransport *self,
                            EvdPeer       *peer)
{
  EvdTransportPeerMessage *msg;

  g_return_val_if_fail (EVD_IS_TRANSPORT (self), NULL);

  msg = g_object_get_data (G_OBJECT (peer), PEER_MSG_KEY);
  if (msg == NULL)
    return NULL;

  if (msg->text_buffer == NULL)
    {
      msg->text_buffer = g_slice_alloc (msg->size + 1);
      msg->text_buffer[msg->size] = '\0';
      memcpy (msg->text_buffer, msg->buffer, msg->size);
    }

  return msg->text_buffer;
}

gboolean
evd_transport_peer_is_connected (EvdTransport *self,
                                 EvdPeer       *peer)
{
  g_return_val_if_fail (EVD_IS_TRANSPORT (self), FALSE);
  g_return_val_if_fail (EVD_IS_PEER (peer), FALSE);

  return EVD_TRANSPORT_GET_INTERFACE (self)->peer_is_connected (self, peer);
}

gboolean
evd_transport_close_peer (EvdTransport  *self,
                          EvdPeer       *peer,
                          gboolean       gracefully,
                          GError       **error)
{
  EvdTransportInterface *iface;
  EvdPeerManager *peer_manager;
  gpointer bag;

  g_return_val_if_fail (EVD_IS_TRANSPORT (self), FALSE);
  g_return_val_if_fail (EVD_IS_PEER (peer), FALSE);

  bag = g_object_get_data (G_OBJECT (peer), PEER_CLOSING_KEY);
  if (bag != NULL)
    return TRUE;

  g_object_ref (self);
  g_object_set_data (G_OBJECT (peer), PEER_CLOSING_KEY, self);

  g_object_ref (peer);

  peer_manager = EVD_TRANSPORT_GET_INTERFACE (self)->peer_manager;
  evd_peer_manager_close_peer (peer_manager, peer, gracefully);

  evd_peer_close (peer, gracefully);

  iface = EVD_TRANSPORT_GET_INTERFACE (self);

  if (iface->peer_closed != NULL)
    iface->peer_closed (self, peer, gracefully);

  evd_transport_notify_peer_closed (self, peer, gracefully);

  g_object_set_data (G_OBJECT (peer), PEER_CLOSING_KEY, NULL);
  g_object_unref (self);

  g_object_unref (peer);

  return TRUE;
}

EvdPeer *
evd_transport_create_new_peer (EvdTransport *self)
{
  EvdTransportInterface *iface;

  g_return_val_if_fail (EVD_IS_TRANSPORT (self), NULL);

  iface = EVD_TRANSPORT_GET_INTERFACE (self);

  g_assert (iface->create_new_peer != NULL);

  return iface->create_new_peer (self);
}

EvdPeer *
evd_transport_lookup_peer (EvdTransport *self, const gchar *peer_id)
{
  EvdPeerManager *peer_manager;
  EvdPeer *peer = NULL;

  peer_manager = EVD_TRANSPORT_GET_INTERFACE (self)->peer_manager;
  peer = evd_peer_manager_lookup_peer (peer_manager, peer_id);

  if (peer != NULL)
    {
      EvdTransport *transport;

      g_object_get (peer, "transport", &transport, NULL);

      if (transport != self)
        peer = NULL;

      g_object_unref (transport);
    }

  return peer;
}
