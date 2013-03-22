/*
 * evd-pki-privkey.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2011-2013, Igalia S.L.
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

#include <gnutls/gnutls.h>

#include "evd-pki-privkey.h"

#include "evd-error.h"

G_DEFINE_TYPE (EvdPkiPrivkey, evd_pki_privkey, G_TYPE_OBJECT)

#define EVD_PKI_PRIVKEY_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                          EVD_TYPE_PKI_PRIVKEY, \
                                          EvdPkiPrivkeyPrivate))

/* private data */
struct _EvdPkiPrivkeyPrivate
{
  gnutls_privkey_t key;

  EvdPkiKeyType type;
};

typedef struct
{
  EvdPkiKeyType key_type;
  guint bits;
} GenKeyData;

/* properties */
enum
{
  PROP_0,
  PROP_TYPE
};

static void     evd_pki_privkey_class_init         (EvdPkiPrivkeyClass *class);
static void     evd_pki_privkey_init               (EvdPkiPrivkey *self);

static void     evd_pki_privkey_finalize           (GObject *obj);

static void     evd_pki_privkey_get_property       (GObject    *obj,
                                                    guint       prop_id,
                                                    GValue     *value,
                                                    GParamSpec *pspec);

static void
evd_pki_privkey_class_init (EvdPkiPrivkeyClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->finalize = evd_pki_privkey_finalize;
  obj_class->get_property = evd_pki_privkey_get_property;

  /* install properties */
  g_object_class_install_property (obj_class, PROP_TYPE,
                                   g_param_spec_uint ("type",
                                                      "Key type",
                                                      "The type of private key (RSA, DSA, etc)",
                                                      EVD_PKI_KEY_TYPE_UNKNOWN,
                                                      EVD_PKI_KEY_TYPE_DSA,
                                                      EVD_PKI_KEY_TYPE_UNKNOWN,
                                                      G_PARAM_READABLE |
                                                      G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (obj_class, sizeof (EvdPkiPrivkeyPrivate));
}

static void
evd_pki_privkey_init (EvdPkiPrivkey *self)
{
  EvdPkiPrivkeyPrivate *priv;

  priv = EVD_PKI_PRIVKEY_GET_PRIVATE (self);
  self->priv = priv;

  priv->key = NULL;

  self->priv->type = EVD_PKI_KEY_TYPE_UNKNOWN;
}

static void
evd_pki_privkey_finalize (GObject *obj)
{
  EvdPkiPrivkey *self = EVD_PKI_PRIVKEY (obj);

  if (self->priv->key != NULL)
    gnutls_privkey_deinit (self->priv->key);

  G_OBJECT_CLASS (evd_pki_privkey_parent_class)->finalize (obj);
}

static void
evd_pki_privkey_get_property (GObject    *obj,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  EvdPkiPrivkey *self;

  self = EVD_PKI_PRIVKEY (obj);

  switch (prop_id)
    {
    case PROP_TYPE:
      g_value_set_uint (value, self->priv->type);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
decrypt_in_thread (GSimpleAsyncResult *res,
                   GObject            *object,
                   GCancellable       *cancellable)
{
  EvdPkiPrivkey *self = EVD_PKI_PRIVKEY (object);
  gint err_code;
  GError *error = NULL;
  gnutls_datum_t *data;
  gnutls_datum_t *msg;

  data = g_simple_async_result_get_op_res_gpointer (res);
  msg = g_new (gnutls_datum_t, 1);

  err_code = gnutls_privkey_decrypt_data (self->priv->key,
                                          0,
                                          data,
                                          msg);
  if (evd_error_propagate_gnutls (err_code, &error))
    {
      g_simple_async_result_take_error (res, error);
      g_free (msg);
    }
  else
    {
      g_simple_async_result_set_op_res_gpointer (res, msg, g_free);
    }

  g_object_unref (res);
}

/* public methods */

EvdPkiPrivkey *
evd_pki_privkey_new (void)
{
  return g_object_new (EVD_TYPE_PKI_PRIVKEY, NULL);
}

EvdPkiKeyType
evd_pki_privkey_get_key_type (EvdPkiPrivkey *self)
{
  g_return_val_if_fail (EVD_IS_PKI_PRIVKEY (self), -1);

  return self->priv->type;
}

/**
 * evd_pki_privkey_import_native:
 * @privkey_st: (type guintptr):
 *
 **/
gboolean
evd_pki_privkey_import_native (EvdPkiPrivkey     *self,
                               gnutls_privkey_t   privkey,
                               GError           **error)
{
  gint err_code;
  guint bits;
  EvdPkiKeyType type;

  g_return_val_if_fail (EVD_IS_PKI_PRIVKEY (self), FALSE);
  g_return_val_if_fail (privkey != NULL, FALSE);

  /* @TODO: check if there are operations pending and return error if so */

  type = gnutls_privkey_get_pk_algorithm (privkey, &bits);
  if (type < 0 && evd_error_propagate_gnutls (err_code, error))
    return FALSE;

  if (self->priv->key != NULL)
    gnutls_privkey_deinit (self->priv->key);

  self->priv->key = privkey;
  self->priv->type = type;

  return TRUE;
}

void
evd_pki_privkey_decrypt (EvdPkiPrivkey       *self,
                         const gchar         *data,
                         gsize                size,
                         GCancellable        *cancellable,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
  GSimpleAsyncResult *res;
  gnutls_datum_t *dec_data;

  g_return_if_fail (EVD_IS_PKI_PRIVKEY (self));

  res = g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   evd_pki_privkey_decrypt);

  if (self->priv->key == NULL)
    {
      g_simple_async_result_set_error (res,
                                       G_IO_ERROR,
                                       G_IO_ERROR_NOT_INITIALIZED,
                                       "Private key not initialized");
      g_simple_async_result_complete_in_idle (res);
      g_object_unref (res);
      return;
    }

  dec_data = g_new (gnutls_datum_t, 1);
  dec_data->data = (guchar *) data;
  dec_data->size = size;

  g_simple_async_result_set_op_res_gpointer (res, dec_data, g_free);

  /* @TODO: use a thread pool to avoid overhead */
  g_simple_async_result_run_in_thread (res,
                                       decrypt_in_thread,
                                       G_PRIORITY_DEFAULT,
                                       cancellable);
}

gchar *
evd_pki_privkey_decrypt_finish (EvdPkiPrivkey  *self,
                                GAsyncResult   *result,
                                gsize          *size,
                                GError        **error)
{
  GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (result);

  g_return_val_if_fail (EVD_IS_PKI_PRIVKEY (self), NULL);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                                                        G_OBJECT (self),
                                                        evd_pki_privkey_decrypt),
                        NULL);

  if (! g_simple_async_result_propagate_error (res, error))
    {
      gnutls_datum_t *msg;

      msg = g_simple_async_result_get_op_res_gpointer (res);

      if (size != NULL)
        *size = msg->size;

      return (gchar *) msg->data;
    }
  else
    return NULL;
}
