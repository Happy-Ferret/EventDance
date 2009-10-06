/*
 * evd-socket.c
 *
 * EventDance project - An event distribution framework (http://eventdance.org)
 *
 * Copyright (C) 2009, Igalia S.L.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 3 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <sys/types.h>
#include <sys/socket.h>

#include "evd-socket-manager.h"
#include "evd-socket.h"
#include "marshal.h"

#define DEFAULT_CONNECT_TIMEOUT 0 /* no timeout */
#define DOMAIN_QUARK_STRING     "org.eventdance.glib.socket"

G_DEFINE_TYPE (EvdSocket, evd_socket, G_TYPE_SOCKET)

#define EVD_SOCKET_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
	                             EVD_TYPE_SOCKET, \
                                     EvdSocketPrivate))

/* private data */
struct _EvdSocketPrivate
{
  EvdSocketState  status;
  GMainContext   *context;

  GClosure *on_read_closure;

  gboolean initable_init;

  guint         connect_timeout;
  guint         connect_timeout_src_id;
  GCancellable *connect_cancellable;
};

/* signals */
enum
{
  CLOSE,
  CONNECT,
  LISTEN,
  NEW_CONNECTION,
  CONNECT_TIMEOUT,
  LAST_SIGNAL
};

static guint evd_socket_signals [LAST_SIGNAL] = { 0 };

/* properties */
enum
{
  PROP_0,
  PROP_READ_CLOSURE,
  PROP_CONNECT_TIMEOUT
};

static void     evd_socket_class_init         (EvdSocketClass *class);
static void     evd_socket_init               (EvdSocket *self);

static void     evd_socket_finalize           (GObject *obj);
static void     evd_socket_dispose            (GObject *obj);

static void     evd_socket_set_property       (GObject      *obj,
					       guint         prop_id,
					       const GValue *value,
					       GParamSpec   *pspec);
static void     evd_socket_get_property       (GObject    *obj,
					       guint       prop_id,
					       GValue     *value,
					       GParamSpec *pspec);

static gboolean evd_socket_event_list_handler (gpointer data);

static gboolean evd_socket_watch              (EvdSocket  *self,
					       GError    **error);
static gboolean evd_socket_unwatch            (EvdSocket  *self,
					       GError    **error);

static void     evd_socket_set_read_closure_internal (EvdSocket *self,
						      GClosure  *closure);
static void     evd_socket_invoke_on_read     (EvdSocket *self);


static void
evd_socket_class_init (EvdSocketClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_socket_dispose;
  obj_class->finalize = evd_socket_finalize;
  obj_class->get_property = evd_socket_get_property;
  obj_class->set_property = evd_socket_set_property;

  class->event_handler = NULL;

  /* install signals */
  evd_socket_signals[CLOSE] =
    g_signal_new ("close",
		  G_TYPE_FROM_CLASS (obj_class),
		  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		  G_STRUCT_OFFSET (EvdSocketClass, close),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__BOXED,
		  G_TYPE_NONE, 1,
		  EVD_TYPE_SOCKET);

  evd_socket_signals[CONNECT] =
    g_signal_new ("connect",
		  G_TYPE_FROM_CLASS (obj_class),
		  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		  G_STRUCT_OFFSET (EvdSocketClass, connect),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__BOXED,
		  G_TYPE_NONE, 1,
		  EVD_TYPE_SOCKET);

  evd_socket_signals[LISTEN] =
    g_signal_new ("listen",
		  G_TYPE_FROM_CLASS (obj_class),
		  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		  G_STRUCT_OFFSET (EvdSocketClass, listen),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__BOXED,
		  G_TYPE_NONE, 1,
		  EVD_TYPE_SOCKET);

  evd_socket_signals[NEW_CONNECTION] =
    g_signal_new ("new-connection",
		  G_TYPE_FROM_CLASS (obj_class),
		  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		  G_STRUCT_OFFSET (EvdSocketClass, new_connection),
		  NULL, NULL,
		  evd_marshal_VOID__BOXED_BOXED,
		  G_TYPE_NONE, 2,
		  EVD_TYPE_SOCKET,
		  EVD_TYPE_SOCKET);

  evd_socket_signals[CONNECT_TIMEOUT] =
    g_signal_new ("connect-timeout",
		  G_TYPE_FROM_CLASS (obj_class),
		  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		  G_STRUCT_OFFSET (EvdSocketClass, connect_timeout),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__BOXED,
		  G_TYPE_NONE, 1,
		  EVD_TYPE_SOCKET);

  /* install properties */
  g_object_class_install_property (obj_class,
                                   PROP_READ_CLOSURE,
                                   g_param_spec_boxed ("read-handler",
                                   "Read closure",
                                   "The callback closure that will be invoked when data is ready to be read",
			           G_TYPE_CLOSURE,
                                   G_PARAM_READWRITE));

  g_object_class_install_property (obj_class,
                                   PROP_CONNECT_TIMEOUT,
                                   g_param_spec_uint ("connect-timeout",
                                   "Connect timeout",
                                   "The timeout in seconds to wait for a connect operation",
			           0,
			           G_MAXUINT,
			           DEFAULT_CONNECT_TIMEOUT,
                                   G_PARAM_READWRITE));

  /* add private structure */
  g_type_class_add_private (obj_class, sizeof (EvdSocketPrivate));

  evd_socket_manager_set_callback (evd_socket_event_list_handler);
}

static void
evd_socket_init (EvdSocket *self)
{
  EvdSocketPrivate *priv;

  priv = EVD_SOCKET_GET_PRIVATE (self);
  self->priv = priv;

  /* initialize private members */
  priv->connect_timeout = DEFAULT_CONNECT_TIMEOUT;
  priv->connect_cancellable = NULL;

  priv->initable_init = FALSE;
  priv->status = EVD_SOCKET_CLOSED;
  priv->context = g_main_context_get_thread_default ();
  priv->on_read_closure = NULL;
}

static void
evd_socket_dispose (GObject *obj)
{
  EvdSocket *self = EVD_SOCKET (obj);

  if (self->priv->connect_cancellable != NULL)
    {
      g_object_unref (self->priv->connect_cancellable);
      self->priv->connect_cancellable = NULL;
    }

  G_OBJECT_CLASS (evd_socket_parent_class)->dispose (obj);
}

static void
evd_socket_finalize (GObject *obj)
{
  G_OBJECT_CLASS (evd_socket_parent_class)->finalize (obj);
}

static void
evd_socket_set_property (GObject      *obj,
			 guint         prop_id,
			 const GValue *value,
			 GParamSpec   *pspec)
{
  EvdSocket *self;

  self = EVD_SOCKET (obj);

  switch (prop_id)
    {
    case PROP_READ_CLOSURE:
      {
	GClosure *closure;

	closure = (GClosure *) g_value_get_boxed (value);
	if (closure != NULL)
	  evd_socket_set_read_closure_internal (self, closure);
	break;
      }

    case PROP_CONNECT_TIMEOUT:
      self->priv->connect_timeout = g_value_get_uint (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_socket_get_property (GObject    *obj,
			 guint       prop_id,
			 GValue     *value,
			 GParamSpec *pspec)
{
  EvdSocket *self;

  self = EVD_SOCKET (obj);

  switch (prop_id)
    {
    case PROP_READ_CLOSURE:
      g_value_set_boxed (value, self->priv->on_read_closure);
      break;

    case PROP_CONNECT_TIMEOUT:
      g_value_set_uint (value, self->priv->connect_timeout);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_socket_set_status (EvdSocket *self, EvdSocketState status)
{
  self->priv->status = status;
}

static gboolean
evd_socket_event_handler (gpointer data)
{
  EvdSocketEvent *event = (EvdSocketEvent *) data;
  EvdSocket *socket;
  GIOCondition condition;
  EvdSocketClass *class;
  GError *error = NULL;

  socket = event->socket;
  condition = event->condition;

  class = EVD_SOCKET_GET_CLASS (socket);
  if (class->event_handler != NULL)
    class->event_handler (socket, condition);
  else
    {
      if (socket->priv->status == EVD_SOCKET_LISTENING)
	{
	  EvdSocket *client;

	  if ((client = evd_socket_accept (socket, &error)) != NULL)
	    {
	      /* TODO: allow external function to decide whether to
		 accept/refuse the new connection */

	      /* fire 'new-connection' signal */
	      g_signal_emit (socket,
			     evd_socket_signals[NEW_CONNECTION],
			     0,
			     client, NULL);
	    }
	  else
	    {
	      /* TODO: Handle error */
	    }
	}
      else
	{
	  if (condition & G_IO_ERR)
	    {
	      evd_socket_close (socket, &error);

	      /* TODO: emit 'error' signal */

	      return FALSE;
	    }

	  if (condition & G_IO_HUP)
	    {
	      evd_socket_close (socket, &error);
	      return FALSE;
	    }

	  if (condition & G_IO_OUT)
	    {
	      if (socket->priv->status == EVD_SOCKET_CONNECTING)
		{
		  evd_socket_set_status (socket, EVD_SOCKET_CONNECTED);

		  /* remove any connect_timeout src */
		  if (socket->priv->connect_timeout_src_id > 0)
		    g_source_remove (socket->priv->connect_timeout_src_id);

		  /* emit 'connected' signal */
		  g_signal_emit (socket, evd_socket_signals[CONNECT], 0, NULL);
		}
	    }

	  if (condition & G_IO_IN)
	    {
	      evd_socket_invoke_on_read (socket);
	    }
	}
    }

  return FALSE;
}

static gboolean
evd_socket_event_list_handler (gpointer data)
{
  GQueue *queue = data;
  gpointer msg;

  while ( (msg = g_queue_pop_head (queue)) != NULL)
    {
      evd_socket_event_handler (msg);
      g_free (msg);
    }

  g_queue_free (queue);

  return FALSE;
}

static gboolean
evd_socket_watch (EvdSocket *self, GError **error)
{
  return evd_socket_manager_add_socket (self, error);
}

static gboolean
evd_socket_unwatch (EvdSocket *self, GError **error)
{
  return evd_socket_manager_del_socket (self, error);
}

static gboolean
evd_socket_initable_init (EvdSocket *self, GError **error)
{
  if (! self->priv->initable_init)
    {
      *error = NULL;

      if (g_initable_init (G_INITABLE (self), NULL, error))
	{
	  g_socket_set_blocking (G_SOCKET (self), FALSE);
	  g_socket_set_keepalive (G_SOCKET (self), TRUE);

	  self->priv->initable_init = TRUE;

	  return TRUE;
	}
    }

  return FALSE;
}

static void
evd_socket_set_read_closure_internal (EvdSocket  *self,
				      GClosure  *closure)
{
  if (self->priv->on_read_closure != NULL)
    g_closure_unref (self->priv->on_read_closure);

  self->priv->on_read_closure = g_closure_ref (closure);
  g_closure_sink (closure);
}

static void
evd_socket_invoke_on_read (EvdSocket *self)
{
  if (self->priv->on_read_closure != NULL)
    {
      GValue params = { 0, };

      g_value_init (&params, EVD_TYPE_SOCKET);
      g_value_set_object (&params, self);

      g_object_ref (self);
      g_closure_invoke (self->priv->on_read_closure, NULL, 1, &params, NULL);
      g_object_unref (self);

      g_value_unset (&params);
    }
}

static gboolean
evd_socket_connect_timeout (gpointer user_data)
{
  EvdSocket *self = EVD_SOCKET (user_data);
  GError *error = NULL;

  /* emit 'connect-timeout' signal*/
  g_signal_emit (self, evd_socket_signals[CONNECT_TIMEOUT], 0, NULL);

  self->priv->connect_timeout_src_id = 0;
  if (! evd_socket_close (self, &error))
    {
      /* TODO: emit 'error' signal */
      return FALSE;
    }

  return FALSE;
}

/* public methods */

EvdSocket *
evd_socket_new (GSocketFamily     family,
		GSocketType       type,
		GSocketProtocol   protocol,
		GError          **error)
{
  EvdSocket *self;

  self = g_object_new (EVD_TYPE_SOCKET,
		       "family", family,
		       "type", type,
		       "protocol", protocol,
		       NULL);

  if (evd_socket_initable_init (self, error))
    return self;
  else
    return NULL;
}

EvdSocket *
evd_socket_new_from_fd (gint     fd,
			GError **error)
{
  EvdSocket *self;

  g_return_val_if_fail (fd > 0, NULL);

  self = g_object_new (EVD_TYPE_SOCKET,
		       "fd", fd,
		       NULL);

  if (evd_socket_initable_init (self, error))
    return self;
  else
    return NULL;
}

GMainContext *
evd_socket_get_context (EvdSocket *self)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), NULL);

  return self->priv->context;
}

gboolean
evd_socket_close (EvdSocket *self, GError **error)
{
  gboolean result = TRUE;

  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);

  if (! evd_socket_unwatch (self, error))
    result = FALSE;

  evd_socket_set_status (self, EVD_SOCKET_CLOSED);

  if (! g_socket_is_closed (G_SOCKET (self)))
    {
      if (! g_socket_close (G_SOCKET (self), error))
	result = FALSE;

      /* fire 'close' signal */
      g_signal_emit (self, evd_socket_signals[CLOSE], 0, NULL);
    }

  return result;
}

gboolean
evd_socket_bind (EvdSocket       *self,
		 GSocketAddress  *address,
		 gboolean         allow_reuse,
		 GError         **error)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);
  g_return_val_if_fail (G_IS_SOCKET_ADDRESS (address), FALSE);

  evd_socket_initable_init (self, error);
  if (*error != NULL)
    return FALSE;

  return g_socket_bind (G_SOCKET (self),
			address,
			allow_reuse,
			error);
}

gboolean
evd_socket_listen (EvdSocket *self, GError **error)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);

  evd_socket_initable_init (self, error);
  if (*error != NULL)
    return FALSE;

  if (g_socket_listen (G_SOCKET (self), error))
    if (evd_socket_watch (self, error))
      {
	self->priv->status = EVD_SOCKET_LISTENING;

	g_signal_emit (self, evd_socket_signals[LISTEN], 0, NULL);
	return TRUE;
      }

  return FALSE;
}

EvdSocket *
evd_socket_accept (EvdSocket *self, GError **error)
{
  gint fd;
  gint client_fd;
  EvdSocket *client = NULL;

  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);

  fd = g_socket_get_fd (G_SOCKET (self));

  client_fd = accept (fd, NULL, 0);

  if ( (client = evd_socket_new_from_fd (client_fd, error)) != NULL)
    {
      if (evd_socket_watch (client, error))
	{
	  evd_socket_set_status (client, EVD_SOCKET_CONNECTED);
	  return client;
	}
    }

  return NULL;
}

gboolean
evd_socket_connect_to (EvdSocket        *self,
		       GSocketAddress   *address,
		       GError          **error)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);
  g_return_val_if_fail (G_IS_SOCKET_ADDRESS (address), FALSE);

  evd_socket_initable_init (self, error);
  if (*error != NULL)
    return FALSE;

  /* if socket not closed, close it first */
  if (self->priv->status != EVD_SOCKET_CLOSED)
    if (! evd_socket_close (self, error))
      return FALSE;

  /* launch connect timeout */
  if (self->priv->connect_timeout > 0)
    self->priv->connect_timeout_src_id =
      g_timeout_add (self->priv->connect_timeout * 1000,
		     (GSourceFunc) evd_socket_connect_timeout,
		     (gpointer) self);

  if (self->priv->connect_cancellable == NULL)
    self->priv->connect_cancellable = g_cancellable_new ();

  if (! g_socket_connect (G_SOCKET (self),
			  address,
			  self->priv->connect_cancellable,
			  error))
    {
      /* an error ocurred, but error-pending
	 is normal as on async ops */
      if ((*error)->code != G_IO_ERROR_PENDING)
	return FALSE;
    }

  /* g_socket_connect returns TRUE on a non-blocking socket, however
     fills error with "connection in progress" hint */
  if (*error != NULL)
    {
      g_error_free (*error);
      *error = NULL;
    }

  evd_socket_set_status (self, EVD_SOCKET_CONNECTING);
  if (evd_socket_watch (self, error))
    return TRUE;

  evd_socket_set_status (self, EVD_SOCKET_CLOSED);
  return FALSE;
}

gboolean
evd_socket_cancel_connect (EvdSocket *self, GError **error)
{
  if (self->priv->status == EVD_SOCKET_CONNECTING)
    {
      if (self->priv->connect_timeout_src_id > 0)
	g_source_remove (self->priv->connect_timeout_src_id);

      g_cancellable_cancel (self->priv->connect_cancellable);

      return evd_socket_close (self, error);
    }
  else
    {
      *error = g_error_new (g_quark_from_static_string (DOMAIN_QUARK_STRING),
			    EVD_SOCKET_ERROR_NOT_CONNECTING,
			    "Socket is not connecting");

      return FALSE;
    }
}

void
evd_socket_set_read_handler (EvdSocket            *self,
			     EvdSocketReadHandler  handler,
			     gpointer              user_data)
{
  GClosure *closure;

  closure = g_cclosure_new (G_CALLBACK (handler),
			    user_data,
			    NULL);

  if (G_CLOSURE_NEEDS_MARSHAL (closure))
    {
      GClosureMarshal marshal = g_cclosure_marshal_VOID__VOID;

      g_closure_set_marshal (closure, marshal);
    }

  evd_socket_set_read_closure_internal (self, closure);
}

void
evd_socket_set_read_closure (EvdSocket *self,
			     GClosure  *closure)
{
  evd_socket_set_read_closure_internal (self, closure);
}
