/*
 * evd-tls-session.c
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

#include "evd-tls-common.h"
#include "evd-tls-session.h"
#include "evd-tls-certificate.h"

#define DOMAIN_QUARK_STRING "org.eventdance.lib.tls-session"

#define EVD_TLS_SESSION_DEFAULT_PRIORITY "NORMAL"

G_DEFINE_TYPE (EvdTlsSession, evd_tls_session, G_TYPE_OBJECT)

#define EVD_TLS_SESSION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                          EVD_TYPE_TLS_SESSION, \
                                          EvdTlsSessionPrivate))

/* private data */
struct _EvdTlsSessionPrivate
{
  GQuark err_domain;

  gnutls_session_t session;
  EvdTlsCredentials *cred;

  EvdTlsMode mode;

  EvdTlsSessionPullFunc pull_func;
  EvdTlsSessionPushFunc push_func;
  gpointer              pull_push_user_data;

  gchar *priority;

  gint cred_ready_sig_id;

  gboolean cred_bound;

  gboolean require_peer_cert;
};


/* properties */
enum
{
  PROP_0,
  PROP_CREDENTIALS,
  PROP_MODE,
  PROP_PRIORITY,
  PROP_REQUIRE_PEER_CERT
};

static void     evd_tls_session_class_init         (EvdTlsSessionClass *class);
static void     evd_tls_session_init               (EvdTlsSession *self);

static void     evd_tls_session_finalize           (GObject *obj);
static void     evd_tls_session_dispose            (GObject *obj);

static void     evd_tls_session_set_property       (GObject      *obj,
                                                    guint         prop_id,
                                                    const GValue *value,
                                                    GParamSpec   *pspec);
static void     evd_tls_session_get_property       (GObject    *obj,
                                                    guint       prop_id,
                                                    GValue     *value,
                                                    GParamSpec *pspec);

static void
evd_tls_session_class_init (EvdTlsSessionClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_tls_session_dispose;
  obj_class->finalize = evd_tls_session_finalize;
  obj_class->get_property = evd_tls_session_get_property;
  obj_class->set_property = evd_tls_session_set_property;

  /* install properties */
  g_object_class_install_property (obj_class, PROP_CREDENTIALS,
                                   g_param_spec_object ("credentials",
                                                        "The SSL/TLS session's credentials",
                                                        "The certificate credentials object to use by this SSL/TLS session",
                                                        EVD_TYPE_TLS_CREDENTIALS,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_MODE,
                                   g_param_spec_uint ("mode",
                                                      "SSL/TLS session mode",
                                                      "The SSL/TLS session's mode of operation: client or server",
                                                      EVD_TLS_MODE_SERVER,
                                                      EVD_TLS_MODE_CLIENT,
                                                      EVD_TLS_MODE_SERVER,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_PRIORITY,
                                   g_param_spec_string ("priority",
                                                        "Priority string to use in this session",
                                                        "Gets/sets the priorities to use on the ciphers, key exchange methods, macs and compression methods",
                                                        EVD_TLS_SESSION_DEFAULT_PRIORITY,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_REQUIRE_PEER_CERT,
                                   g_param_spec_boolean ("require-peer-cert",
                                                         "Require peer certificate",
                                                         "Controls whether a peer certificate will be requested during handshake",
                                                         FALSE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_STRINGS));

  /* add private structure */
  g_type_class_add_private (obj_class, sizeof (EvdTlsSessionPrivate));
}

static void
evd_tls_session_init (EvdTlsSession *self)
{
  EvdTlsSessionPrivate *priv;

  priv = EVD_TLS_SESSION_GET_PRIVATE (self);
  self->priv = priv;

  priv->err_domain = g_quark_from_static_string (DOMAIN_QUARK_STRING);

  priv->session = NULL;
  priv->cred = NULL;

  priv->mode = EVD_TLS_MODE_SERVER;

  priv->pull_func = NULL;
  priv->push_func = NULL;
  priv->pull_push_user_data = NULL;

  priv->priority = g_strdup (EVD_TLS_SESSION_DEFAULT_PRIORITY);

  priv->cred_ready_sig_id = 0;

  priv->cred_bound = FALSE;

  priv->require_peer_cert = FALSE;
}

static void
evd_tls_session_dispose (GObject *obj)
{
  EvdTlsSession *self = EVD_TLS_SESSION (obj);

  if (self->priv->cred != NULL)
    {
      g_object_unref (self->priv->cred);
      self->priv->cred = NULL;
    }

  G_OBJECT_CLASS (evd_tls_session_parent_class)->dispose (obj);
}

static void
evd_tls_session_finalize (GObject *obj)
{
  EvdTlsSession *self = EVD_TLS_SESSION (obj);

  if (self->priv->session != NULL)
    gnutls_deinit (self->priv->session);

  if (self->priv->priority != NULL)
    g_free (self->priv->priority);

  G_OBJECT_CLASS (evd_tls_session_parent_class)->finalize (obj);
}

static void
evd_tls_session_set_property (GObject      *obj,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  EvdTlsSession *self;

  self = EVD_TLS_SESSION (obj);

  switch (prop_id)
    {
    case PROP_CREDENTIALS:
      evd_tls_session_set_credentials (self, g_value_get_object (value));
      break;

    case PROP_MODE:
      self->priv->mode = g_value_get_uint (value);
      break;

    case PROP_PRIORITY:
      if (self->priv->priority != NULL)
        g_free (self->priv->priority);
      self->priv->priority = g_strdup (g_value_get_string (value));
      break;

    case PROP_REQUIRE_PEER_CERT:
      self->priv->require_peer_cert = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_tls_session_get_property (GObject    *obj,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  EvdTlsSession *self;

  self = EVD_TLS_SESSION (obj);

  switch (prop_id)
    {
    case PROP_CREDENTIALS:
      g_value_set_object (value, evd_tls_session_get_credentials (self));
      break;

    case PROP_MODE:
      g_value_set_uint (value, self->priv->mode);
      break;

    case PROP_PRIORITY:
      g_value_set_string (value, self->priv->priority);
      break;

    case PROP_REQUIRE_PEER_CERT:
      g_value_set_boolean (value, self->priv->require_peer_cert);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static gssize
evd_tls_session_push (gnutls_transport_ptr_t  ptr,
                      const void             *buf,
                      gsize                   size)
{
  EvdTlsSession *self = EVD_TLS_SESSION (ptr);

  return self->priv->push_func (self,
                                buf,
                                size,
                                self->priv->pull_push_user_data);
}

static gssize
evd_tls_session_pull (gnutls_transport_ptr_t  ptr,
                      void                   *buf,
                      gsize                   size)
{
  EvdTlsSession *self = EVD_TLS_SESSION (ptr);

  return self->priv->pull_func (self,
                                buf,
                                size,
                                self->priv->pull_push_user_data);
}

static gboolean
evd_tls_session_handshake_internal (EvdTlsSession  *self,
                                    GError        **error)
{
  gint err_code;

  err_code = gnutls_handshake (self->priv->session);
  if (err_code == GNUTLS_E_SUCCESS)
    return TRUE;
  else
    if (gnutls_error_is_fatal (err_code) == 1)
      evd_tls_build_error (err_code, error, self->priv->err_domain);

  return FALSE;
}

static gboolean
evd_tls_session_bind_credentials (EvdTlsSession      *self,
                                  EvdTlsCredentials  *cred,
                                  GError            **error)
{
  gpointer cred_internal;

  /* set credentials */
  cred_internal = evd_tls_credentials_get_credentials (cred);

  if (cred_internal == NULL)
    {
      /* TODO: return error, credentials not prepared */

      return FALSE;
    }
  else if (evd_tls_credentials_get_anonymous (cred))
    {
      if (self->priv->mode == EVD_TLS_MODE_SERVER)
        gnutls_credentials_set (self->priv->session,
                              GNUTLS_CRD_ANON,
                              (gnutls_anon_server_credentials_t) cred_internal);
      else
        gnutls_credentials_set (self->priv->session,
                              GNUTLS_CRD_ANON,
                              (gnutls_anon_client_credentials_t) cred_internal);
    }
  else
    {
      gnutls_credentials_set (self->priv->session,
                              GNUTLS_CRD_CERTIFICATE,
                              (gnutls_certificate_credentials_t) cred_internal);
    }

  self->priv->cred_bound = TRUE;

  return TRUE;
}

static void
evd_tls_session_on_credentials_ready (EvdTlsCredentials *cred,
                                      gpointer           user_data)
{
  EvdTlsSession *self = EVD_TLS_SESSION (user_data);
  GError *error = NULL;

  if (! evd_tls_session_bind_credentials (self, cred, &error))
    {
      /* TODO: handle error */
      g_debug ("error binding credentials");
    }
  else if (! evd_tls_session_handshake_internal (self, &error))
    {
      if (error != NULL)
        {
          /* TODO: raise error asynchronously, by firing 'error' signal */
          g_debug ("handshake error!: %s", error->message);
        }
    }
}

static gboolean
evd_tls_session_shutdown (EvdTlsSession           *self,
                          gnutls_close_request_t   how,
                          GError                 **error)
{
  g_return_val_if_fail (EVD_IS_TLS_SESSION (self), FALSE);

  if (self->priv->session != NULL)
    {
      gint err_code;

      err_code = gnutls_bye (self->priv->session, how);
      if (err_code < 0 && gnutls_error_is_fatal (err_code) != 0)
        {
          evd_tls_build_error (err_code, error, self->priv->err_domain);
          return FALSE;
        }
    }

  return TRUE;
}

/* public methods */

EvdTlsSession *
evd_tls_session_new (void)
{
  EvdTlsSession *self;

  self = g_object_new (EVD_TYPE_TLS_SESSION, NULL);

  return self;
}

void
evd_tls_session_set_credentials (EvdTlsSession     *self,
                                 EvdTlsCredentials *credentials)
{
  g_return_if_fail (EVD_IS_TLS_SESSION (self));
  g_return_if_fail (EVD_IS_TLS_CREDENTIALS (credentials));

  if (self->priv->cred != NULL)
    {
      if (self->priv->cred_ready_sig_id != 0)
        {
          g_signal_handler_disconnect (self->priv->cred,
                                       self->priv->cred_ready_sig_id);

          self->priv->cred_ready_sig_id = 0;
        }

      g_object_unref (self->priv->cred);
    }

  self->priv->cred = credentials;
  g_object_ref (self->priv->cred);
}

EvdTlsCredentials *
evd_tls_session_get_credentials (EvdTlsSession *self)
{
  g_return_val_if_fail (EVD_IS_TLS_SESSION (self), NULL);

  if (self->priv->cred == NULL)
    {
      self->priv->cred = evd_tls_credentials_new ();
      g_object_ref_sink (self->priv->cred);
    }

  return self->priv->cred;
}

void
evd_tls_session_set_transport_funcs (EvdTlsSession         *self,
                                     EvdTlsSessionPullFunc  pull_func,
                                     EvdTlsSessionPushFunc  push_func,
                                     gpointer               user_data)
{
  g_return_if_fail (EVD_IS_TLS_SESSION (self));
  g_return_if_fail (pull_func != NULL);
  g_return_if_fail (push_func != NULL);

  self->priv->pull_func = pull_func;
  self->priv->push_func = push_func;
  self->priv->pull_push_user_data = user_data;
}

gboolean
evd_tls_session_handshake (EvdTlsSession  *self,
                           GError        **error)
{
  EvdTlsCredentials *cred;
  gint err_code;

  g_return_val_if_fail (EVD_IS_TLS_SESSION (self), FALSE);

  if (self->priv->session == NULL)
    {
      err_code = gnutls_init (&self->priv->session, self->priv->mode);
      if (err_code != GNUTLS_E_SUCCESS)
        {
          evd_tls_build_error (err_code, error, self->priv->err_domain);
          return FALSE;
        }
      else
        {
          err_code = gnutls_priority_set_direct (self->priv->session,
                                                 self->priv->priority,
                                                 NULL);

          if (err_code != GNUTLS_E_SUCCESS)
            {
              evd_tls_build_error (err_code, error, self->priv->err_domain);
              return FALSE;
            }

          if (self->priv->require_peer_cert &&
              self->priv->mode == EVD_TLS_MODE_SERVER)
            {
              gnutls_certificate_server_set_request (self->priv->session,
                                                     GNUTLS_CERT_REQUEST);
            }

          gnutls_transport_set_ptr2 (self->priv->session, self, self);
          gnutls_transport_set_push_function (self->priv->session,
                                              evd_tls_session_push);
          gnutls_transport_set_pull_function (self->priv->session,
                                              evd_tls_session_pull);

          cred = evd_tls_session_get_credentials (self);
          if (! evd_tls_credentials_ready (cred))
            {
              if (self->priv->cred_ready_sig_id == 0)
                self->priv->cred_ready_sig_id =
                  g_signal_connect (cred,
                                    "ready",
                                    G_CALLBACK (evd_tls_session_on_credentials_ready),
                                    self);

              evd_tls_credentials_prepare (cred, self->priv->mode, error);

              return FALSE;
            }
          else
            {
              if (! evd_tls_session_bind_credentials (self, cred, error))
                return FALSE;
            }
        }
    }

  if (self->priv->cred_bound)
    return evd_tls_session_handshake_internal (self, error);
  else
    return FALSE;
}

gssize
evd_tls_session_read (EvdTlsSession  *self,
                      gchar          *buffer,
                      gsize           size,
                      GError        **error)
{
  gssize result;

  g_return_val_if_fail (EVD_IS_TLS_SESSION (self), -1);
  g_return_val_if_fail (size > 0, -1);
  g_return_val_if_fail (buffer != NULL, -1);

  result = gnutls_record_recv (self->priv->session, buffer, size);

  //  g_debug ("record_recv result: %d", result);

  if (result < 0)
    {
      if (gnutls_error_is_fatal (result) != 0)
        {
          evd_tls_build_error (result, error, self->priv->err_domain);
          result = -1;
        }
      else
        {
          result = 0;
        }
    }

  return result;
}

gssize
evd_tls_session_write (EvdTlsSession  *self,
                       const gchar    *buffer,
                       gsize           size,
                       GError        **error)
{
  gssize result;

  g_return_val_if_fail (EVD_IS_TLS_SESSION (self), -1);
  g_return_val_if_fail (size > 0, -1);
  g_return_val_if_fail (buffer != NULL, -1);

  result = gnutls_record_send (self->priv->session, buffer, size);

  //  g_debug ("record_send result: %d", result);

  if (result < 0)
    {
      if (gnutls_error_is_fatal (result) != 0)
        {
          evd_tls_build_error (result, error, self->priv->err_domain);
          result = -1;
        }
      else
        {
          result = 0;
        }
    }

  return result;
}

GIOCondition
evd_tls_session_get_direction (EvdTlsSession *self)
{
  g_return_val_if_fail (EVD_IS_TLS_SESSION (self), 0);
  g_return_val_if_fail (self->priv->session != NULL, 0);

  if (gnutls_record_get_direction (self->priv->session) == 0)
    return G_IO_IN;
  else
    return G_IO_OUT;
}

gboolean
evd_tls_session_close (EvdTlsSession *self, GError **error)
{
  return evd_tls_session_shutdown (self, GNUTLS_SHUT_RDWR, error);
}

gboolean
evd_tls_session_shutdown_write (EvdTlsSession *self, GError **error)
{
  return evd_tls_session_shutdown (self, GNUTLS_SHUT_WR, error);
}

void
evd_tls_session_copy_properties (EvdTlsSession *self,
                                 EvdTlsSession *target)
{
  g_return_if_fail (EVD_IS_TLS_SESSION (self));
  g_return_if_fail (self != target);

  g_object_set (target,
                "credentials", evd_tls_session_get_credentials (self),
                "priority", self->priv->priority,
                "require-peer-cert", self->priv->require_peer_cert,
                NULL);
}

GList *
evd_tls_session_get_peer_certificates (EvdTlsSession *self, GError **error)
{
  GList *list = NULL;

  const gnutls_datum_t *raw_certs_list;
  guint raw_certs_len;

  g_return_val_if_fail (EVD_IS_TLS_SESSION (self), NULL);

  if (self->priv->session == NULL)
    return NULL;

  raw_certs_list = gnutls_certificate_get_peers (self->priv->session, &raw_certs_len);
  if (raw_certs_list != NULL)
    {
      guint i;
      EvdTlsCertificate *cert;

      for (i=0; i<raw_certs_len; i++)
        {
          cert = evd_tls_certificate_new ();

          if (! evd_tls_certificate_import (cert,
                                            (gchar *) raw_certs_list[i].data,
                                            raw_certs_list[i].size,
                                            error))
            {
              evd_tls_free_certificates (list);

              return NULL;
            }
          else
            {
              list = g_list_append (list, (gpointer) cert);
            }
        }
    }

  return list;
}

gint
evd_tls_session_verify_peer (EvdTlsSession  *self,
                             guint           flags,
                             GError        **error)
{
  gint result = EVD_TLS_VERIFY_STATE_OK;
  guint status;
  gint err_code;
  GList *peer_certs;

  g_return_val_if_fail (EVD_IS_TLS_SESSION (self), -1);

  if (self->priv->session == NULL)
    {
      if (error != NULL)
        *error = g_error_new (self->priv->err_domain,
                              EVD_TLS_ERROR_SESS_NOT_INITIALIZED,
                              "SSL/TLS session not yet initialized");

      return -1;
    }

  /* basic verification */
  err_code = gnutls_certificate_verify_peers2 (self->priv->session, &status);
  if (err_code != GNUTLS_E_SUCCESS)
    {
      if (err_code != GNUTLS_E_NO_CERTIFICATE_FOUND)
        {
          evd_tls_build_error (err_code, error, self->priv->err_domain);

          return -1;
        }
      else
        {
          result |= EVD_TLS_VERIFY_STATE_NO_CERT;
        }
    }
  else
    {
      if (status & GNUTLS_CERT_INVALID)
        result |= EVD_TLS_VERIFY_STATE_INVALID;
      if (status & GNUTLS_CERT_REVOKED)
        result |= EVD_TLS_VERIFY_STATE_REVOKED;
      if (status & GNUTLS_CERT_SIGNER_NOT_FOUND)
        result |= EVD_TLS_VERIFY_STATE_SIGNER_NOT_FOUND;
      if (status & GNUTLS_CERT_SIGNER_NOT_CA)
        result |= EVD_TLS_VERIFY_STATE_SIGNER_NOT_CA;
      if (status & GNUTLS_CERT_INSECURE_ALGORITHM)
        result |= EVD_TLS_VERIFY_STATE_INSECURE_ALG;
    }

  /* check each peer certificate individually */
  peer_certs = evd_tls_session_get_peer_certificates (self, error);
  if (peer_certs != NULL)
    {
      GList *node;
      EvdTlsCertificate *cert;
      gint cert_result;

      node = peer_certs;
      do
        {
          cert = EVD_TLS_CERTIFICATE (node->data);

          cert_result = evd_tls_certificate_verify_validity (cert, error);
          if (cert_result < 0)
            break;
          else
            result |= cert_result;

          node = node->next;
        }
      while (node != NULL);

      evd_tls_free_certificates (peer_certs);
    }

  return result;
}
