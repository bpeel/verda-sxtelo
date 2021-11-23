/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2021  Neil Roberts
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>

#include "vsx-connection.h"

typedef struct
{
  GInetAddress *inet_address;
  GSocketAddress *socket_address;
  VsxConversationSet *conversation_set;
  VsxPersonSet *person_set;
  VsxConnection *conn;
} Harness;

static char
ws_request[] =
  "GET / HTTP/1.1\r\n"
  "Sec-WebSocket-Key: potato\r\n"
  "\r\n";

static char
ws_reply[] =
  "HTTP/1.1 101 Switching Protocols\r\n"
  "Upgrade: websocket\r\n"
  "Connection: Upgrade\r\n"
  "Sec-WebSocket-Accept: p4PX7Zjj5DyJVCBrt49wxR4RyoQ=\r\n"
  "\r\n";

static Harness *
create_harness(void)
{
  Harness *harness = g_new0 (Harness, 1);

  harness->inet_address = g_inet_address_new_loopback (G_SOCKET_FAMILY_IPV4);
  harness->socket_address =
    g_inet_socket_address_new (harness->inet_address, 5344);

  harness->person_set = vsx_person_set_new ();
  harness->conversation_set = vsx_conversation_set_new ();

  harness->conn = vsx_connection_new (harness->socket_address,
                                      harness->conversation_set,
                                      harness->person_set);

  return harness;
}

static void
free_harness(Harness *harness)
{
  vsx_connection_free (harness->conn);
  vsx_object_unref (harness->conversation_set);
  vsx_object_unref (harness->person_set);
  g_object_unref (harness->socket_address);
  g_object_unref (harness->inet_address);

  g_free (harness);
}

static Harness *
create_negotiated_harness (void)
{
  Harness *harness = create_harness ();

  GError *error = NULL;

  if (!vsx_connection_parse_data (harness->conn,
                                  (guint8 *) ws_request,
                                  (sizeof ws_request) - 1,
                                  &error))
    {
      fprintf (stderr,
               "Unexpected error negotiating WebSocket: %s",
               error->message);
      g_error_free (error);
      free_harness (harness);

      return NULL;
    }

  guint8 buf[(sizeof ws_reply) * 2];
  size_t got = vsx_connection_fill_output_buffer (harness->conn,
                                                  buf,
                                                  sizeof buf);

  if (got != (sizeof ws_reply) - 1
      || memcmp (ws_reply, buf, got))
    {
      fprintf (stderr,
               "WebSocket negotation dosen’t match.\n"
               "Received:\n"
               "%.*s\n"
               "Expected:\n"
               "%s\n",
               (int) got,
               buf,
               ws_reply);
      free_harness (harness);
      return NULL;
    }

  return harness;
}

int
main (int argc, char **argv)
{
  int ret = EXIT_SUCCESS;

  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    {
      ret = EXIT_FAILURE;
    }
  else
    {
      free_harness (harness);
    }

  return ret;
}