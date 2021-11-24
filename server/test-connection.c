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

typedef struct
{
  const char *frame;
  size_t frame_length;
  const char *expected_message;
} FrameErrorTest;

#define BIN_STR(x) x, (sizeof (x)) - 1

static const FrameErrorTest
frame_error_tests[] =
  {
    {
      BIN_STR("\x82\x1\x42"),
      "Client sent an unknown message ID (0x42)"
    },
    {
      BIN_STR("\x8f\x3HI!"),
      "Client sent an unknown control frame"
    },
    {
      BIN_STR("\x92\x1\x42"),
      "Client sent a frame with non-zero RSV bits",
    },
    {
      BIN_STR("\xa2\x1\x42"),
      "Client sent a frame with non-zero RSV bits",
    },
    {
      BIN_STR("\x88\x7e\x00\x7eggggggggggggggggggggggggggggggggggggggggggggg"
              "ggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggg"
              "gggggggggggggggggggg"),
      "Client sent a control frame (0x8) that is too long (126)"
    },
    {
      BIN_STR("\x08\x1!"),
      "Client sent a fragmented control frame"
    },
    {
      BIN_STR("\x82\x7e\x04\x01 This has a length of 1025 …"),
      "Client sent a message (0x2) that is too long (1025)"
    },
    {
      BIN_STR("\x00\x1!"),
      "Client sent a continuation frame without starting a message"
    },
    {
      BIN_STR("\x00\x1!"),
      "Client sent a continuation frame without starting a message"
    },
    {
      BIN_STR("\x02\x0"),
      "Client sent an empty fragmented message"
    },
    {
      BIN_STR("\x83\x1!"),
      "Client sent a frame opcode (0x3) which the server doesn’t understand"
    },
    {
      BIN_STR("\x82\x0"),
      "Client sent an empty message"
    },
    {
      BIN_STR("\x82\x9\x80no_name\0"),
      "Invalid new player command received"
    },
    {
      BIN_STR("\x82\x12\x80gefault\0Zamenhof\0"
              "\x82\x12\x80gefault\0Zamenhof\0"),
      "Client sent a new player request but already specified a player"
    },
  };

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

static gboolean
test_frame_errors(void)
{
  gboolean ret = TRUE;

  for (int i = 0; i < G_N_ELEMENTS (frame_error_tests); i++)
    {
      Harness *harness = create_negotiated_harness ();

      if (harness == NULL)
        return FALSE;

      GError *error = NULL;

      if (vsx_connection_parse_data (harness->conn,
                                     (guint8 *) frame_error_tests[i].frame,
                                     frame_error_tests[i].frame_length,
                                     &error))
        {
          fprintf (stderr,
                   "frame error test %i: "
                   "error expected but parsing succeeded\n",
                   i);
          ret = FALSE;
        }
      else
        {
          if (strcmp (error->message, frame_error_tests[i].expected_message))
            {
              fprintf (stderr,
                       "frame error test %i: "
                       "expected error message does not match received one\n"
                       " Expected: %s\n"
                       " Received: %s\n",
                       i,
                       frame_error_tests[i].expected_message,
                       error->message);
              ret = FALSE;
            }
          g_error_free (error);
        }

      free_harness (harness);
    }

  return ret;
}

static gboolean
test_eof_before_ws (void)
{
  Harness *harness = create_harness ();

  gboolean ret = TRUE;
  GError *error = NULL;

  if (vsx_connection_parse_eof (harness->conn, &error))
    {
      fprintf (stderr,
               "test_eof_before_ws: Parsing EOF succeeded but expected "
               "to fail\n");
      ret = FALSE;
    }
  else
    {
      char *expected_message = ("Client closed the connection before "
                                "finishing WebSocket negotiation");
      if (strcmp (error->message, expected_message))
        {
          fprintf (stderr,
                   "test_eof_before_ws: Error message differs:\n"
                   " Expected: %s\n"
                   " Received: %s\n",
                   expected_message,
                   error->message);
          ret = FALSE;
        }
      g_error_free (error);
    }

  free_harness (harness);

  return ret;
}

static gboolean
test_close_in_frame (void)
{
  static const char *tests[] =
    {
      /* Unfinished frame */
      "\x82\x5!",
      /* Unfinished fragmented message */
      "\x02\x1!",
    };

  gboolean ret = TRUE;

  for (int i = 0; i < G_N_ELEMENTS (tests); i++)
    {
      Harness *harness = create_negotiated_harness ();

      if (harness == NULL)
        return FALSE;

      GError *error = NULL;

      if (!vsx_connection_parse_data (harness->conn,
                                      (guint8 *) tests[i],
                                      strlen (tests[i]),
                                      &error))
        {
          fprintf (stderr,
                   "test_close_in_frame: %i: Parsing failed when success "
                   "expected: %s",
                   i,
                   error->message);
          g_error_free (error);
          ret = FALSE;
        }
      else if (vsx_connection_parse_eof (harness->conn, &error))
        {
          fprintf (stderr,
                   "test_close_in_frame: %i: Parsing EOF succeeded but "
                   "expected to fail\n",
                   i);
          ret = FALSE;
        }
      else
        {
          char *expected_message = ("Client closed the connection in the "
                                    "middle of a frame");

          if (strcmp (error->message, expected_message))
            {
              fprintf (stderr,
                       "test_close_in_frame: %i: Error message differs:\n"
                       " Expected: %s\n"
                       " Received: %s\n",
                       i,
                       expected_message,
                       error->message);
              ret = FALSE;
            }
          g_error_free (error);
        }

      free_harness (harness);
    }

  return ret;
}

static gboolean
create_player (Harness *harness,
               const char *room_name,
               const char *player_name,
               VsxConversation **conversation_out,
               VsxPlayer **player_out)
{
  GString *buf = g_string_new (NULL);

  g_string_append_c (buf, 0x82);
  g_string_append_c (buf, strlen (room_name) + strlen (player_name) + 3);
  g_string_append_c (buf, 0x80);
  g_string_append (buf, room_name);
  g_string_append_c (buf, 0);
  g_string_append (buf, player_name);
  g_string_append_c (buf, 0);

  gboolean ret = TRUE;
  GError *error = NULL;

  if (!vsx_connection_parse_data (harness->conn,
                                  (guint8 *) buf->str,
                                  buf->len,
                                  &error))
    {
      fprintf (stderr,
               "Unexpected error while creating new player: %s\n",
               error->message);
      g_error_free (error);
      ret = FALSE;
    }
  else
    {
      VsxConversation *conversation =
        vsx_conversation_set_get_conversation (harness->conversation_set,
                                               room_name);

      if (conversation->n_players < 1)
        {
          fprintf (stderr,
                   "The conversation is empty after creating a player\n");
          ret = FALSE;
        }
      else
        {
          VsxPlayer *player =
            conversation->players[conversation->n_players - 1];

          if (strcmp (player->name, player_name))
            {
              fprintf (stderr,
                       "The player name does not match:\n"
                       " Expected: %s\n"
                       " Received: %s\n",
                       player_name,
                       player->name);
              ret = FALSE;
            }
          else
            {
              if (conversation_out)
                *conversation_out = vsx_object_ref (conversation);
              if (player_out)
                *player_out = vsx_object_ref (player);
            }
        }

      vsx_object_unref (conversation);
    }

  g_string_free (buf, TRUE);

  return ret;
}

static gboolean
test_new_player (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return FALSE;

  gboolean ret = create_player (harness,
                                "default:eo", "Zamenhof",
                                NULL, /* conversation_out */
                                NULL /* player_out */);

  free_harness (harness);

  return ret;
}

int
main (int argc, char **argv)
{
  int ret = EXIT_SUCCESS;

  if (!test_frame_errors ())
    ret = EXIT_FAILURE;

  if (!test_eof_before_ws ())
    ret = EXIT_FAILURE;

  if (!test_close_in_frame ())
    ret = EXIT_FAILURE;

  if (!test_new_player ())
    ret = EXIT_FAILURE;

  return ret;
}
