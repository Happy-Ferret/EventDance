/*
 * test-pki.c
 *
 * EventDance project - An event distribution framework (http://eventdance.org)
 *
 * Copyright (C) 2011-2013, Igalia S.L.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 */

#include <string.h>
#include <evd.h>
#include <glib.h>

const gchar *msg = "This is a secret message";

typedef struct
{
  gchar *test_name;

  gchar *cert_filename;
  gchar *key_filename;
  EvdPkiKeyType key_type;

  gint error_code;
  GQuark error_domain;
} TestCase;

typedef struct
{
  EvdTlsCertificate *cert;
  EvdTlsPrivkey *cert_key;

  EvdPkiPrivkey *privkey;
  EvdPkiPubkey *pubkey;

  GMainLoop *main_loop;
  gchar *enc_data;
  gchar *out_data;
  gchar *signature;
  gsize sig_size;
  const TestCase *test_case;
} Fixture;

const TestCase test_cases[] =
{
  {
    "X.509/RSA",
    TESTS_DIR "certs/x509-server.pem",
    TESTS_DIR "certs/x509-server-key.pem",
    EVD_PKI_KEY_TYPE_RSA,
    0, 0
  },

  /* @TODO: OpenPGP private key is failing in GnuTLS when exporting RSA params.
  This is likely caused by the way GnuPG exports private keys, making the secret
  part unusable */
  /*
  {
    "OpenPGP/RSA",
    TESTS_DIR "certs/openpgp-server.asc",
    TESTS_DIR "certs/openpgp-server-key.asc",
    EVD_PKI_KEY_TYPE_RSA,
    0, 0
  }
  */
};

void
fixture_setup (Fixture       *f,
               gconstpointer  test_data)
{
  f->test_case = test_data;

  f->main_loop = g_main_loop_new (NULL, FALSE);

  f->enc_data = NULL;
  f->out_data = NULL;
}

void
fixture_teardown (Fixture       *f,
                  gconstpointer  test_data)
{
  g_free (f->out_data);
  g_free (f->enc_data);

  g_main_loop_unref (f->main_loop);

  if (f->cert != NULL)
    g_object_unref (f->cert);
  if (f->cert_key != NULL)
    g_object_unref (f->cert_key);

  if (f->privkey != NULL)
    g_object_unref (f->privkey);
  if (f->pubkey != NULL)
    g_object_unref (f->pubkey);
  g_free (f->signature);
}

static gboolean
compare_strings (const gchar *s1, const gchar *s2, gsize len)
{
  gsize i;

  for (i = 0; i < len; i++)
    if (s1[i] != s2[i])
      return FALSE;

  return TRUE;
}

static gboolean
quit (gpointer user_data)
{
  g_main_loop_quit (user_data);

  return FALSE;
}

static void
test_privkey_basic (Fixture       *f,
                    gconstpointer  test_data)
{
  EvdPkiKeyType type;

  f->privkey = evd_pki_privkey_new ();
  g_assert (EVD_IS_PKI_PRIVKEY (f->privkey));

  type = evd_pki_privkey_get_key_type (f->privkey);
  g_assert_cmpint (type, ==, EVD_PKI_KEY_TYPE_UNKNOWN);

  g_object_get (f->privkey, "type", &type, NULL);
  g_assert_cmpint (type, ==, EVD_PKI_KEY_TYPE_UNKNOWN);
}

static void
test_pubkey_basic (Fixture       *f,
                   gconstpointer  test_data)
{
  EvdPkiKeyType type;

  f->pubkey = evd_pki_pubkey_new ();
  g_assert (EVD_IS_PKI_PUBKEY (f->pubkey));

  type = evd_pki_pubkey_get_key_type (f->pubkey);
  g_assert_cmpint (type, ==, EVD_PKI_KEY_TYPE_UNKNOWN);

  g_object_get (f->pubkey, "type", &type, NULL);
  g_assert_cmpint (type, ==, EVD_PKI_KEY_TYPE_UNKNOWN);
}

static gboolean
load_cert_and_key (Fixture     *f,
                   const gchar *cert_filename,
                   const gchar *key_filename)
{
  gchar *data;
  GError *error = NULL;
  gsize len;
  EvdPkiKeyType key_type;

  /* load TLS certificate */
  g_file_get_contents (cert_filename, &data, &len, &error);
  g_assert_no_error (error);

  f->cert = evd_tls_certificate_new ();
  evd_tls_certificate_import (f->cert, data, len, &error);
  g_assert_no_error (error);

  g_free (data);

  /* load TLS private key */
  g_file_get_contents (key_filename, &data, &len, &error);
  g_assert_no_error (error);

  f->cert_key = evd_tls_privkey_new ();
  evd_tls_privkey_import (f->cert_key, data, len, &error);
  g_assert_no_error (error);

  g_free (data);

  /* get PKI public key from certificate */
  f->pubkey = evd_tls_certificate_get_pki_key (f->cert, &error);
  g_assert_no_error (error);

  g_assert (EVD_IS_PKI_PUBKEY (f->pubkey));

  /* get PKI private key from certificate key */
  f->privkey = evd_tls_privkey_get_pki_key (f->cert_key, &error);
  g_assert_no_error (error);

  g_assert (EVD_IS_PKI_PRIVKEY (f->privkey));

  /* validate key type */
  key_type = evd_pki_privkey_get_key_type (f->privkey);
  g_assert_cmpint (key_type, ==, f->test_case->key_type);

  key_type = evd_pki_pubkey_get_key_type (f->pubkey);
  g_assert_cmpint (key_type, ==, f->test_case->key_type);

  return TRUE;
}

static void
privkey_on_decrypt (GObject      *obj,
                    GAsyncResult *res,
                    gpointer      user_data)
{
  Fixture *f = user_data;
  GError *error = NULL;
  gsize size;

  f->out_data = evd_pki_privkey_decrypt_finish (EVD_PKI_PRIVKEY (obj),
                                                res,
                                                &size,
                                                &error);

  if (f->test_case->error_code == 0)
    {
      g_assert_no_error (error);
      g_assert (f->out_data != NULL);
      g_assert_cmpint (size, ==, strlen (msg));
      g_assert (compare_strings (f->out_data, msg, size));
    }
  else
    {
      g_assert_error (error,
                      f->test_case->error_domain,
                      f->test_case->error_code);
    }

  g_idle_add (quit, f->main_loop);
}

static void
pubkey_on_encrypt (GObject      *obj,
                   GAsyncResult *res,
                   gpointer      user_data)
{
  Fixture *f = user_data;
  GError *error = NULL;
  gsize size;

  f->enc_data = evd_pki_pubkey_encrypt_finish (EVD_PKI_PUBKEY (obj),
                                               res,
                                               &size,
                                               &error);
  if (f->test_case->error_code == 0)
    {
      g_assert_no_error (error);
      g_assert (f->enc_data != NULL);
      g_assert_cmpint (size, >, 0);

      evd_pki_privkey_decrypt (f->privkey,
                               f->enc_data,
                               size,
                               NULL,
                               privkey_on_decrypt,
                               f);
    }
  else
    {
      g_assert_error (error,
                      f->test_case->error_domain,
                      f->test_case->error_code);

      g_idle_add (quit, f->main_loop);
    }
}

static void
test_pubkey_encrypt (Fixture       *f,
                     gconstpointer  test_data)
{
  load_cert_and_key (f,
                     f->test_case->cert_filename,
                     f->test_case->key_filename);

  evd_pki_pubkey_encrypt (f->pubkey,
                          msg,
                          strlen (msg),
                          NULL,
                          pubkey_on_encrypt,
                          f);

  g_main_loop_run (f->main_loop);
}

static void
on_privkey_generated (GObject      *obj,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  Fixture *f = user_data;
  GError *error = NULL;
  gboolean result;

  g_assert (obj != NULL && EVD_IS_PKI_PRIVKEY (obj));
  g_assert (obj == G_OBJECT (f->privkey));

  result = evd_pki_privkey_generate_finish (EVD_PKI_PRIVKEY (obj),
                                            res,
                                            &error);
  g_assert_no_error (error);
  g_assert (result);

  f->pubkey = evd_pki_privkey_get_public_key (f->privkey, &error);
  g_assert_no_error (error);
  g_assert (f->pubkey != NULL && EVD_IS_PKI_PUBKEY (f->pubkey));

  test_pubkey_encrypt (f, f->test_case);

  g_idle_add (quit, f->main_loop);
}

static void
test_gen_key_pair (Fixture       *f,
                   gconstpointer  test_data)
{
  f->privkey = evd_pki_privkey_new ();

  evd_pki_privkey_generate (f->privkey,
                            f->test_case->key_type,
                            1024,
                            NULL,
                            on_privkey_generated,
                            f);

  g_main_loop_run (f->main_loop);
}

static void
pubkey_on_verify (GObject      *obj,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  EvdPkiPubkey *key;
  GError *error = NULL;
  Fixture *f = user_data;
  gboolean verification;

  g_assert (EVD_IS_PKI_PUBKEY (obj));
  g_assert (G_IS_SIMPLE_ASYNC_RESULT (result));

  key = EVD_PKI_PUBKEY (obj);

  verification = evd_pki_pubkey_verify_data_finish (key, result, &error);

  g_assert_no_error (error);
  g_assert (verification == TRUE);

  g_main_loop_quit (f->main_loop);
}

static void
pubkey_verify (Fixture *f)
{
  evd_pki_pubkey_verify_data (f->pubkey,
                              msg,
                              strlen (msg),
                              f->signature,
                              f->sig_size,
                              NULL,
                              pubkey_on_verify,
                              f);
}

static void
privkey_on_sign (GObject      *obj,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  EvdPkiPrivkey *key;
  GError *error = NULL;
  Fixture *f = user_data;

  g_assert (EVD_IS_PKI_PRIVKEY (obj));
  g_assert (G_IS_SIMPLE_ASYNC_RESULT (result));

  key = EVD_PKI_PRIVKEY (obj);

  f->signature = evd_pki_privkey_sign_data_finish (key,
                                                   result,
                                                   &f->sig_size,
                                                   &error);

  g_assert_no_error (error);
  g_assert (f->signature != NULL);

  pubkey_verify (f);
}

static void
test_privkey_sign (Fixture       *f,
                   gconstpointer  test_data)
{
  load_cert_and_key (f,
                     f->test_case->cert_filename,
                     f->test_case->key_filename);

  evd_pki_privkey_sign_data (f->privkey,
                             msg,
                             strlen (msg),
                             NULL,
                             privkey_on_sign,
                             f);

  g_main_loop_run (f->main_loop);
}

gint
main (gint argc, gchar *argv[])
{
  gint i;

  evd_tls_init (NULL);
  g_test_init (&argc, &argv, NULL);

  g_test_add ("/evd/pki/private-key/basic",
              Fixture,
              NULL,
              fixture_setup,
              test_privkey_basic,
              fixture_teardown);

  g_test_add ("/evd/pki/public-key/basic",
              Fixture,
              NULL,
              fixture_setup,
              test_pubkey_basic,
              fixture_teardown);

  for (i = 0; i < sizeof (test_cases) / sizeof (TestCase); i++)
    {
      gchar *test_name;

      /* encrypt with public key, decrypt with private */
      test_name = g_strdup_printf ("/evd/pki/%s/encrypt-decrypt",
                                   test_cases[i].test_name);

      g_test_add (test_name,
                  Fixture,
                  &test_cases[i],
                  fixture_setup,
                  test_pubkey_encrypt,
                  fixture_teardown);

      g_free (test_name);

      /* sign with private key, verify with public */
      test_name = g_strdup_printf ("/evd/pki/%s/sign-verify",
                                   test_cases[i].test_name);

      g_test_add (test_name,
                  Fixture,
                  &test_cases[i],
                  fixture_setup,
                  test_privkey_sign,
                  fixture_teardown);

      g_free (test_name);
    }

  /* generate RSA key-pair */
  g_test_add ("/evd/pki/generate-key-pair/RSA",
              Fixture,
              &test_cases[0],
              fixture_setup,
              test_gen_key_pair,
              fixture_teardown);

  evd_tls_deinit ();

  return g_test_run ();
}
