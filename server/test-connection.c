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
#include <inttypes.h>

#include "vsx-connection.h"
#include "vsx-proto.h"

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
    {
      BIN_STR("\x82\x5\x81gggg"),
      "Invalid reconnect command received"
    },
    {
      BIN_STR("\x82\x12\x80gefault\0Zamenhof\0"
              "\x82\xb\x81gggggggghh"),
      "Client sent a reconnect request but already specified a player"
    },
    {
      BIN_STR("\x82\xb\x81gggggggghh"),
      "Client tried to reconnect to non-existant player 0x6767676767676767"
    },
    {
      BIN_STR("\x82\x1\x83"),
      "Client sent a command without a person",
    },
    {
      BIN_STR("\x82\x5\x83poop"),
      "Invalid keep alive message received",
    },
    {
      BIN_STR("\x82\x8\x85no-zero"),
      "Invalid send message command received",
    },
    {
      BIN_STR("\x82\x2\x88\x0"),
      "Invalid move tile command received",
    },
    {
      BIN_STR("\x82\x1\x8b"),
      "Invalid set_n_tiles command received",
    }
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

static gboolean
negotiate_connection (VsxConnection *conn)
{
  GError *error = NULL;

  if (!vsx_connection_parse_data (conn,
                                  (guint8 *) ws_request,
                                  (sizeof ws_request) - 1,
                                  &error))
    {
      fprintf (stderr,
               "Unexpected error negotiating WebSocket: %s",
               error->message);
      g_error_free (error);

      return FALSE;
    }

  guint8 buf[(sizeof ws_reply) * 2];
  size_t got = vsx_connection_fill_output_buffer (conn,
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
      return FALSE;
    }

  return TRUE;
}

static Harness *
create_negotiated_harness (void)
{
  Harness *harness = create_harness ();

  if (!negotiate_connection (harness->conn))
    {
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
read_person_id (VsxConnection *conn,
                guint64 *person_id_out,
                guint8 *player_num_out)
{
  guint8 buf[1 + 1 + 1 + sizeof (guint64) + 1];

  size_t got = vsx_connection_fill_output_buffer (conn,
                                                  buf,
                                                  sizeof buf);

  if (got != sizeof buf)
    {
      fprintf (stderr,
               "Only got %zu bytes out of %zu "
               "when trying to read the player ID\n",
               got,
               sizeof buf);
      return FALSE;
    }

  if (buf[2] != VSX_PROTO_PLAYER_ID)
    {
      fprintf (stderr,
               "Expected player ID command but received 0x%02x\n",
               buf[2]);
      return FALSE;
    }

  if (person_id_out)
    {
      memcpy (person_id_out, buf + 3, sizeof *person_id_out);
      *person_id_out = GUINT64_FROM_LE (*person_id_out);
    }

  if (player_num_out)
    *player_num_out = buf[3 + sizeof (guint64)];

  return TRUE;
}

static gboolean
create_player (Harness *harness,
               const char *room_name,
               const char *player_name,
               VsxPerson **person_out)
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
      guint64 person_id;
      guint8 player_num;

      if (!read_person_id (harness->conn, &person_id, &player_num))
        {
          ret = FALSE;
        }
      else
        {
          VsxPerson *person = vsx_person_set_get_person (harness->person_set,
                                                         person_id);

          if (person == NULL)
            {
              fprintf (stderr,
                       "Returned person ID (%" PRIu64 ") doesn’t exist after "
                       "creating player\n",
                       person_id);
              ret = FALSE;
            }
          else if (strcmp (person->player->name, player_name))
            {
              fprintf (stderr,
                       "The player name does not match:\n"
                       " Expected: %s\n"
                       " Received: %s\n",
                       player_name,
                       person->player->name);
              ret = FALSE;
            }
          else if (person->conversation->n_players - 1 != player_num)
            {
              fprintf (stderr,
                       "New player is not last player (%i / %i)\n",
                       player_num,
                       person->conversation->n_players);
              ret = FALSE;
            }
          else
            {
              if (person_out)
                *person_out = vsx_object_ref (person);
            }
        }
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
                                NULL /* person_out */);

  free_harness (harness);

  return ret;
}

static gboolean
reconnect_to_player (VsxConnection *conn,
                     guint64 player_id,
                     guint16 n_messages_received,
                     GError **error)
{
  GString *buf = g_string_new (NULL);

  player_id = GUINT64_TO_LE (player_id);
  n_messages_received = GUINT64_TO_LE (n_messages_received);

  g_string_append_c (buf, 0x82);
  g_string_append_c (buf, 1 + sizeof (guint64) + sizeof (guint16));
  g_string_append_c (buf, 0x81);
  g_string_append_len (buf, (void *) &player_id, sizeof player_id);
  g_string_append_len (buf,
                       (void *) &n_messages_received,
                       sizeof n_messages_received);

  gboolean ret = TRUE;

  if (!vsx_connection_parse_data (conn,
                                  (guint8 *) buf->str,
                                  buf->len,
                                  error))
    {
      ret = FALSE;
    }
  else
    {
      guint64 person_id;

      if (!read_person_id (conn, &person_id, NULL /* player_num */))
        {
          ret = FALSE;
        }
      else
        {
          if (person_id != player_id)
            {
              fprintf (stderr,
                       "After reconnect, received person ID != request ID "
                       "(%" PRIu64 " != %" PRIu64 ")\n",
                       person_id,
                       player_id);
              ret = FALSE;
            }
        }
    }

  g_string_free (buf, TRUE);

  return ret;
}

static gboolean
test_reconnect_ok (Harness *harness,
                   guint64 player_id)
{
  VsxConnection *other_conn = vsx_connection_new (harness->socket_address,
                                                  harness->conversation_set,
                                                  harness->person_set);

  gboolean ret = TRUE;

  if (!negotiate_connection (other_conn))
    {
      ret = FALSE;
    }
  else
    {
      GError *error = NULL;

      if (!reconnect_to_player (other_conn,
                                player_id,
                                0, /* n_messages_received */
                                &error))
        {
          fprintf (stderr,
                   "test_reconnect_ok: Unexpected error: %s\n",
                   error->message);
          g_error_free (error);
          ret = FALSE;
        }
    }

  vsx_connection_free (other_conn);

  return ret;
}

static gboolean
test_reconnect_bad_n_messages_received (Harness *harness,
                                        guint64 player_id)
{
  VsxConnection *other_conn = vsx_connection_new (harness->socket_address,
                                                  harness->conversation_set,
                                                  harness->person_set);

  gboolean ret = TRUE;

  if (!negotiate_connection (other_conn))
    {
      ret = FALSE;
    }
  else
    {
      GError *error = NULL;

      if (reconnect_to_player (other_conn,
                               player_id,
                               10, /* n_messages_received */
                               &error))
        {
          fprintf (stderr,
                   "test_reconnect_bad_n_messages_received: "
                   "Reconnect unexpectedly succeeded\n");
          ret = FALSE;
        }
      else
        {
          const char *expected_message =
            "Client claimed to have received 10 messages but only 0 "
            "are available";

          if (strcmp (error->message, expected_message))
            {
              fprintf (stderr,
                       "test_reconnect_bad_n_messages_received: "
                       "Error message does not match\n"
                       " Expected: %s\n"
                       " Received: %s\n",
                       expected_message,
                       error->message);
              ret = FALSE;
            }

          g_error_free (error);
        }
    }

  vsx_connection_free (other_conn);

  return ret;
}

static gboolean
test_reconnect (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return FALSE;

  VsxPerson *person;
  gboolean ret = TRUE;

  if (!create_player (harness,
                      "default:eo", "Zamenhof",
                      &person))
    {
      ret = FALSE;
    }
  else
    {
      if (!test_reconnect_ok (harness, person->id))
        ret = FALSE;

      if (!test_reconnect_bad_n_messages_received (harness, person->id))
        ret = FALSE;

      vsx_object_unref (person);
    }

  free_harness (harness);

  return ret;
}

static gboolean
test_keep_alive (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return FALSE;

  VsxPerson *person;
  gboolean ret = TRUE;

  if (!create_player (harness,
                      "default:eo", "Zamenhof",
                      &person))
    {
      ret = FALSE;
    }
  else
    {
      GError *error = NULL;

      if (!vsx_connection_parse_data (harness->conn,
                                      (guint8 *) "\x82\x1\x83",
                                      3,
                                      &error))
        {
          fprintf (stderr,
                   "test_keep_alive: Unexpected error: %s\n",
                   error->message);
          g_error_free (error);

          ret = FALSE;
        }

      vsx_object_unref (person);
    }

  free_harness (harness);

  return ret;
}

static gboolean
test_leave (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return FALSE;

  VsxPerson *person;
  gboolean ret = TRUE;

  if (!create_player (harness,
                      "default:eo", "Zamenhof",
                      &person))
    {
      ret = FALSE;
    }
  else
    {
      GError *error = NULL;

      if (!vsx_connection_parse_data (harness->conn,
                                      (guint8 *) "\x82\x1\x84",
                                      3,
                                      &error))
        {
          fprintf (stderr,
                   "test_leave: Unexpected error: %s\n",
                   error->message);
          g_error_free (error);

          ret = FALSE;
        }
      else if (person->conversation->n_connected_players != 0)
        {
          fprintf (stderr,
                   "test_leave: The conversation still has %i players after "
                   "leave command sent\n",
                   person->conversation->n_connected_players);
          ret = FALSE;
        }

      vsx_object_unref (person);
    }

  free_harness (harness);

  return ret;
}

static gboolean
check_expected_message (VsxPerson *person,
                        const char *expected_message)
{
  if (person->conversation->messages->len < 1)
    {
      fprintf (stderr,
               "There are no messages in the conversation after sending a "
               "message\n");
      return FALSE;
    }

  VsxConversationMessage *message =
    &g_array_index (person->conversation->messages,
                    VsxConversationMessage,
                    person->conversation->messages->len - 1);

  if (strcmp (message->raw_text, expected_message))
    {
      fprintf (stderr,
               "Message in conversation does not match message sent.\n"
               " Expected: %s\n"
               " Received: %s\n",
               expected_message,
               message->raw_text);
      return FALSE;
    }

  return TRUE;
}

static gboolean
test_send_one_message (Harness *harness,
                       VsxPerson *person)
{
  GError *error = NULL;
  gboolean ret = TRUE;

  const char *expected_message = "Hello, world!";

  GString *buf = g_string_new (NULL);

  g_string_append_c (buf, 0x82);
  g_string_append_c (buf, strlen (expected_message) + 2);
  g_string_append_c (buf, 0x85);
  g_string_append (buf, expected_message);
  g_string_append_c (buf, '\0');

  if (!vsx_connection_parse_data (harness->conn,
                                  (guint8 *) buf->str, buf->len,
                                  &error))
    {
      fprintf (stderr,
               "Unexpected error when sending message: %s\n",
               error->message);
      g_error_free (error);

      ret = FALSE;
    }
  else if (!check_expected_message (person, expected_message))
    {
      ret = FALSE;
    }

    g_string_free (buf, TRUE);

    return ret;
}

static gboolean
test_send_fragmented_message (Harness *harness,
                              VsxPerson *person)
{
  GError *error = NULL;
  gboolean ret = TRUE;

  const char *expected_message = "Hello, fragmented world!";

  GString *buf = g_string_new (NULL);

  g_string_append_c (buf, 0x85);
  g_string_append (buf, expected_message);
  g_string_append_c (buf, '\0');

  /* Send the message as a series of one-byte fragments */
  for (int i = 0; i < buf->len; i++)
    {
      guint8 frag[] =
        {
          i == 0 ? 0x02
          : i == buf->len - 1 ? 0x80
          : 0x00,
          1,
          buf->str[i]
        };

      if (!vsx_connection_parse_data (harness->conn,
                                      frag,
                                      sizeof frag,
                                      &error))
        {
          fprintf (stderr,
                   "Unexpected error when sending fragmented message: %s\n",
                   error->message);
          g_error_free (error);

          ret = FALSE;

          goto done;
        }
    }

  if (!check_expected_message (person, expected_message))
    ret = FALSE;

 done:
  g_string_free (buf, TRUE);

  return ret;
}

static gboolean
test_send_message (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return FALSE;

  VsxPerson *person;
  gboolean ret = TRUE;

  if (!create_player (harness,
                      "default:eo", "Zamenhof",
                      &person))
    {
      ret = FALSE;
    }
  else
    {
      if (!test_send_one_message (harness, person))
        ret = FALSE;

      if (!test_send_fragmented_message (harness, person))
        ret = FALSE;

      vsx_object_unref (person);
    }

  free_harness (harness);

  return ret;
}

static gboolean
test_typing_commands (Harness *harness, VsxPerson *person)
{
  static const struct
  {
    guint8 command;
    gboolean typing_result;
  } typing_commands[] =
    {
      { VSX_PROTO_STOP_TYPING, FALSE },
      { VSX_PROTO_START_TYPING, TRUE },
      { VSX_PROTO_START_TYPING, TRUE },
      { VSX_PROTO_STOP_TYPING, FALSE },
      { VSX_PROTO_START_TYPING, TRUE },
    };

  for (int i = 0; i < G_N_ELEMENTS (typing_commands); i++)
    {
      GError *error = NULL;

      guint8 buf[] = { 0x82, 0x1, typing_commands[i].command };

      if (!vsx_connection_parse_data (harness->conn,
                                      buf, sizeof buf,
                                      &error))
        {
          fprintf (stderr,
                   "test_typing_commands: %i: Unexpected error: %s\n",
                   i,
                   error->message);
          g_error_free (error);

          return FALSE;
        }

      if (!!(person->player->flags & VSX_PLAYER_TYPING)
          != !!typing_commands[i].typing_result)
        {
          fprintf (stderr,
                   "test_typing_commands: %i: "
                   "Typing status is not as expected\n",
                   i);
          return FALSE;
        }
    }

  return TRUE;
}

static gboolean
test_typing (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return FALSE;

  VsxPerson *person;
  gboolean ret = TRUE;

  if (!create_player (harness,
                      "default:eo", "Zamenhof",
                      &person))
    {
      ret = FALSE;
    }
  else
    {
      if (!test_typing_commands (harness, person))
        {
          ret = FALSE;
        }
      else
        {
          /* Try sending a message. This should automatically set the
           * typing status to FALSE.
           */
          if (!test_send_one_message (harness, person))
            {
              ret = FALSE;
            }
          else if ((person->player->flags & VSX_PLAYER_TYPING))
            {
              fprintf (stderr,
                       "Sending a message did not reset the typing status\n");
              ret = FALSE;
            }
       }

      vsx_object_unref (person);
    }

  free_harness (harness);

  return ret;
}

static gboolean
test_turn_and_move_commands (Harness *harness, VsxPerson *person)
{
  GError *error = NULL;

  if (!vsx_connection_parse_data (harness->conn,
                                  (guint8 *) "\x82\x1\x89",
                                  3,
                                  &error))
    {
      fprintf (stderr,
               "Unexpected error after turn command: %s\n",
               error->message);
      g_error_free (error);

      return FALSE;
    }

  if (person->conversation->n_tiles_in_play != 1)
    {
      fprintf (stderr,
               "After turning a tile, n_tiles_in_play = %i\n",
               person->conversation->n_tiles_in_play);
      return FALSE;
    }

  if (!vsx_connection_parse_data (harness->conn,
                                  (guint8 *) "\x82\x6\x88\x0\xfe\xff\x20\x00",
                                  8,
                                  &error))
    {
      fprintf (stderr,
               "Unexpected error after move command: %s\n",
               error->message);
      g_error_free (error);

      return FALSE;
    }

  if (person->conversation->tiles[0].x != -2 ||
      person->conversation->tiles[0].y != 32)
    {
      fprintf (stderr,
               "After moving a tile to -2,32, it is at %i,%i\n",
               person->conversation->tiles[0].x,
               person->conversation->tiles[0].y);
      return FALSE;
    }

  if (vsx_connection_parse_data (harness->conn,
                                 (guint8 *) "\x82\x6\x88\x1\x10\x00\x20\x00",
                                 8,
                                 &error))
    {
      fprintf (stderr,
               "Unexpected success after trying to move an invalid tile\n");
      return FALSE;
    }

  const char *expected_message =
    "Player tried to move a tile that is not in play";
  gboolean ret = TRUE;

  if (strcmp (error->message, expected_message))
    {
      fprintf (stderr,
               "Error message does not match after trying to move an invalid "
               "tile.\n"
               " Expected: %s\n"
               " Received: %s\n",
               expected_message,
               error->message);
      ret = FALSE;
    }

  g_error_free (error);

  return ret;
}

static gboolean
test_turn_and_move (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return FALSE;

  VsxPerson *person;
  gboolean ret = TRUE;

  if (!create_player (harness,
                      "default:eo", "Zamenhof",
                      &person))
    {
      ret = FALSE;
    }
  else
    {
      if (!test_turn_and_move_commands (harness, person))
        ret = FALSE;

      vsx_object_unref (person);
    }

  free_harness (harness);

  return ret;
}

static gboolean
test_shout (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return FALSE;

  VsxPerson *person;
  gboolean ret = TRUE;

  if (!create_player (harness,
                      "default:eo", "Zamenhof",
                      &person))
    {
      ret = FALSE;
    }
  else
    {
      GError *error = NULL;

      if (!vsx_connection_parse_data (harness->conn,
                                      (guint8 *) "\x82\x1\x8a",
                                      3,
                                      &error))
        {
          fprintf (stderr,
                   "test_shout: Unexpected error: %s\n",
                   error->message);
          g_error_free (error);
          ret = FALSE;
        }
      else if (person->conversation->last_shout_time == 0)
        {
          fprintf (stderr,
                   "test_shout: last_shout_time is still zero after shouting");
          ret = FALSE;
        }

      vsx_object_unref (person);
    }

  free_harness (harness);

  return ret;
}

static gboolean
test_set_n_tiles (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return FALSE;

  VsxPerson *person;
  gboolean ret = TRUE;

  if (!create_player (harness,
                      "default:eo", "Zamenhof",
                      &person))
    {
      ret = FALSE;
    }
  else
    {
      GError *error = NULL;

      if (!vsx_connection_parse_data (harness->conn,
                                      (guint8 *) "\x82\x2\x8b\x5",
                                      4,
                                      &error))
        {
          fprintf (stderr,
                   "test_set_n_tiles: Unexpected error: %s\n",
                   error->message);
          g_error_free (error);
          ret = FALSE;
        }
      else if (person->conversation->total_n_tiles != 5)
        {
          fprintf (stderr,
                   "test_set_n_tiles: failed to set total_n_tiles "
                   "(%i != 5)\n",
                   person->conversation->total_n_tiles);
          ret = FALSE;
        }

      vsx_object_unref (person);
    }

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

  if (!test_reconnect ())
    ret = EXIT_FAILURE;

  if (!test_keep_alive ())
    ret = EXIT_FAILURE;

  if (!test_leave ())
    ret = EXIT_FAILURE;

  if (!test_send_message ())
    ret = EXIT_FAILURE;

  if (!test_typing ())
    ret = EXIT_FAILURE;

  if (!test_turn_and_move ())
    ret = EXIT_FAILURE;

  if (!test_shout ())
    ret = EXIT_FAILURE;

  if (!test_set_n_tiles ())
    ret = EXIT_FAILURE;

  return ret;
}
