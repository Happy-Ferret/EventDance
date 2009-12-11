/*
 * evd-tester.c
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

#include <glib.h>

#include "test-json-filter.c"
#include "test-socket.c"
#include "test-inet-socket.c"
#include "test-json-socket.c"

gint
main (gint argc, gchar **argv)
{
  g_type_init ();
  g_test_init (&argc, &argv, NULL);

  test_socket ();
  test_json_filter ();
  test_json_socket ();
  test_inet_socket ();

  g_test_run ();

  return 0;
}
