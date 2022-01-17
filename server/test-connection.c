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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <string.h>

#include "vsx-connection.h"
#include "vsx-proto.h"
#include "vsx-buffer.h"
#include "vsx-util.h"

typedef struct
{
  struct vsx_netaddress socket_address;
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
      BIN_STR("\x82\x5\x8cgggg"),
      "Invalid new private game command received"
    },
    {
      BIN_STR("\x82\x0d\x8c" "eo\0Zamenhof\0"
              "\x82\x0d\x8c" "eo\0Zamenhof\0"),
      "Client sent a new private game request but already specified a player"
    },
    {
      BIN_STR("\x82\x5\x8dgggg"),
      "Invalid join game command received"
    },
    {
      BIN_STR("\x82\x0d\x8c" "eo\0Zamenhof\0"
              "\x82\x12\x8dggggggggZamenhof\0"),
      "Client sent a join game request but already specified a player"
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
    },
    {
      BIN_STR("\x82\x1\x8e"),
      "Invalid set_language command received",
    },
    {
      BIN_STR("\x82\x13\x80gefault\0Zamenhof\x1b\0"),
      "Client sent an invalid player name",
    },
    {
      BIN_STR("\x82\x0e\x8c" "eo\0Zamenhof\x1b\0"),
      "Client sent an invalid player name",
    },
    {
      BIN_STR("\x82\x13\x8d" "ggggggggZamenhof\x1b\0"),
      "Client sent an invalid player name",
    },
    {
      BIN_STR("\x82\x13\x80gefa\x1bult\0Zamenhof\0"),
      "Client sent an invalid room name",
    },
    {
      BIN_STR("\x82\x7e\x01\x0b\x80gefault\0"
              "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
              "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
              "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
              "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
              "aaaaaaaaa\0"),
      "Client sent an invalid player name",
    },
  };

static Harness *
create_harness(void)
{
  Harness *harness = vsx_calloc (sizeof *harness);

  bool ret = vsx_netaddress_from_string (&harness->socket_address,
                                         "127.0.0.1",
                                         5344);
  assert(ret);

  harness->person_set = vsx_person_set_new ();
  harness->conversation_set = vsx_conversation_set_new ();

  harness->conn = vsx_connection_new (&harness->socket_address,
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

  vsx_free (harness);
}

static bool
negotiate_connection (VsxConnection *conn)
{
  struct vsx_error *error = NULL;

  if (!vsx_connection_parse_data (conn,
                                  (uint8_t *) ws_request,
                                  (sizeof ws_request) - 1,
                                  &error))
    {
      fprintf (stderr,
               "Unexpected error negotiating WebSocket: %s",
               error->message);
      vsx_error_free (error);

      return false;
    }

  uint8_t buf[(sizeof ws_reply) * 2];
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
      return false;
    }

  return true;
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

static bool
test_frame_errors(void)
{
  bool ret = true;

  for (int i = 0; i < VSX_N_ELEMENTS (frame_error_tests); i++)
    {
      Harness *harness = create_negotiated_harness ();

      if (harness == NULL)
        return false;

      struct vsx_error *error = NULL;

      if (vsx_connection_parse_data (harness->conn,
                                     (uint8_t *) frame_error_tests[i].frame,
                                     frame_error_tests[i].frame_length,
                                     &error))
        {
          fprintf (stderr,
                   "frame error test %i: "
                   "error expected but parsing succeeded\n",
                   i);
          ret = false;
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
              ret = false;
            }
          vsx_error_free (error);
        }

      free_harness (harness);
    }

  return ret;
}

static bool
test_eof_before_ws (void)
{
  Harness *harness = create_harness ();

  bool ret = true;
  struct vsx_error *error = NULL;

  if (vsx_connection_parse_eof (harness->conn, &error))
    {
      fprintf (stderr,
               "test_eof_before_ws: Parsing EOF succeeded but expected "
               "to fail\n");
      ret = false;
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
          ret = false;
        }
      vsx_error_free (error);
    }

  free_harness (harness);

  return ret;
}

static bool
test_close_in_frame (void)
{
  static const char *tests[] =
    {
      /* Unfinished frame */
      "\x82\x5!",
      /* Unfinished fragmented message */
      "\x02\x1!",
    };

  bool ret = true;

  for (int i = 0; i < VSX_N_ELEMENTS (tests); i++)
    {
      Harness *harness = create_negotiated_harness ();

      if (harness == NULL)
        return false;

      struct vsx_error *error = NULL;

      if (!vsx_connection_parse_data (harness->conn,
                                      (uint8_t *) tests[i],
                                      strlen (tests[i]),
                                      &error))
        {
          fprintf (stderr,
                   "test_close_in_frame: %i: Parsing failed when success "
                   "expected: %s",
                   i,
                   error->message);
          vsx_error_free (error);
          ret = false;
        }
      else if (vsx_connection_parse_eof (harness->conn, &error))
        {
          fprintf (stderr,
                   "test_close_in_frame: %i: Parsing EOF succeeded but "
                   "expected to fail\n",
                   i);
          ret = false;
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
              ret = false;
            }
          vsx_error_free (error);
        }

      free_harness (harness);
    }

  return ret;
}

static bool
read_player_id (VsxConnection *conn,
                uint64_t *person_id_out,
                uint8_t *player_num_out)
{
  uint8_t buf[1 + 1 + 1 + sizeof (uint64_t) + 1];

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
      return false;
    }

  if (buf[2] != VSX_PROTO_PLAYER_ID)
    {
      fprintf (stderr,
               "Expected player ID command but received 0x%02x\n",
               buf[2]);
      return false;
    }

  if (person_id_out)
    {
      memcpy (person_id_out, buf + 3, sizeof *person_id_out);
      *person_id_out = VSX_UINT64_FROM_LE (*person_id_out);
    }

  if (player_num_out)
    *player_num_out = buf[3 + sizeof (uint64_t)];

  return true;
}

static bool
read_n_tiles (VsxConnection *conn,
              uint8_t *n_tiles_out)
{
  uint8_t buf[1 + 1 + 1 + 1];

  size_t got = vsx_connection_fill_output_buffer (conn,
                                                  buf,
                                                  sizeof buf);

  if (got != sizeof buf)
    {
      fprintf (stderr,
               "Only got %zu bytes out of %zu "
               "when trying to read n_tiles\n",
               got,
               sizeof buf);
      return false;
    }

  if (buf[2] != VSX_PROTO_N_TILES)
    {
      fprintf (stderr,
               "Expected N_TILES command but received 0x%02x\n",
               buf[2]);
      return false;
    }

  if (n_tiles_out)
      *n_tiles_out = buf[3];

  return true;
}

static bool
read_language_code (VsxConnection *conn,
                    const char *expected_language_code)
{
  size_t code_length = strlen (expected_language_code);
  size_t buf_length = 1 + 1 + 1 + code_length + 1;
  uint8_t *buf = alloca (buf_length);

  size_t got = vsx_connection_fill_output_buffer (conn, buf, buf_length);

  if (got != buf_length)
    {
      fprintf (stderr,
               "Only got %zu bytes out of %zu "
               "when trying to read language_code\n",
               got,
               buf_length);
      return false;
    }

  if (buf[2] != VSX_PROTO_LANGUAGE)
    {
      fprintf (stderr,
               "Expected LANGUAGE command but received 0x%02x\n",
               buf[2]);
      return false;
    }

  if (buf[1] != buf_length - 2)
    {
      fprintf (stderr,
               "Expected language command of length %zu but got %i\n",
               buf_length - 2,
               buf[1]);
      return false;
    }

  if (buf[buf_length - 1] != '\0')
    {
      fprintf (stderr,
               "String in language event is not null terminated.\n");
      return false;
    }

  const char *actual_language_code = (const char *) buf + 3;

  if (strcmp (actual_language_code, expected_language_code))
    {
      fprintf (stderr,
               "Language code in language message is wrong.\n"
               " Expected: %s\n"
               " Received: %s\n",
               expected_language_code,
               actual_language_code);
      return false;
    }

  return true;
}

static bool
read_conversation_id (VsxConnection *conn,
                      uint64_t *conversation_id_out)
{
  uint8_t buf[1 + 1 + 1 + 8];

  size_t got = vsx_connection_fill_output_buffer (conn,
                                                  buf,
                                                  sizeof buf);

  if (got != sizeof buf)
    {
      fprintf (stderr,
               "Only got %zu bytes out of %zu "
               "when trying to read conversation ID\n",
               got,
               sizeof buf);
      return false;
    }

  if (buf[2] != VSX_PROTO_CONVERSATION_ID)
    {
      fprintf (stderr,
               "Expected conversation ID command but received 0x%02x\n",
               buf[2]);
      return false;
    }

  if (conversation_id_out)
    {
      memcpy (conversation_id_out, buf + 3, sizeof *conversation_id_out);
      *conversation_id_out = VSX_UINT64_FROM_LE (*conversation_id_out);
    }

  return true;
}

static bool
read_player_name (VsxConnection *conn,
                  int expected_player_num,
                  const char *expected_name)
{
  size_t buf_size = (1 /* frame command */
                     + 1 /* length */
                     + 1 /* command */
                     + 1 /* player_num */
                     + strlen (expected_name) + 1 /* name + terminator */);
  uint8_t *buf = alloca (buf_size);

  size_t got = vsx_connection_fill_output_buffer (conn, buf, buf_size);

  if (got != buf_size)
    {
      fprintf (stderr,
               "read_player_name: Expected %zu bytes but received %zu\n",
               buf_size,
               got);
      return false;
    }

  if (buf[2] != VSX_PROTO_PLAYER_NAME)
    {
      fprintf (stderr,
               "Expected player name command but received 0x%02x\n",
               buf[2]);
      return false;
    }

  if (buf[3] != expected_player_num)
    {
      fprintf (stderr,
               "read_player_name: player_num does not match\n"
               " Expected: %i\n"
               " Received: %i\n",
               expected_player_num,
               buf[3]);
      return false;
    }

  if (memcmp (buf + 4, expected_name, strlen (expected_name) + 1))
    {
      fprintf (stderr,
               "read_player_name: name does not match\n"
               " Expected: %s\n"
               " Received: %.*s\n",
               expected_name,
               (int) strlen (expected_name),
               buf + 4);
      return false;
    }

  return true;
}

static bool
read_player (VsxConnection *conn,
             int expected_player_num,
             int expected_flags)
{
  uint8_t buf[1 /* frame command */
              + 1 /* length */
              + 1 /* command */
              + 1 /* player_num */
              + 1 /* flags */];

  size_t got = vsx_connection_fill_output_buffer (conn, buf, sizeof buf);

  if (got != sizeof buf)
    {
      fprintf (stderr,
               "read_player: Expected %zu bytes but received %zu\n",
               sizeof buf,
               got);
      return false;
    }

  if (buf[2] != VSX_PROTO_PLAYER)
    {
      fprintf (stderr,
               "Expected player command but received 0x%02x\n",
               buf[2]);
      return false;
    }

  if (buf[3] != expected_player_num)
    {
      fprintf (stderr,
               "read_player: player_num does not match\n"
               " Expected: %i\n"
               " Received: %i\n",
               expected_player_num,
               buf[3]);
      return false;
    }

  if (buf[4] != expected_flags)
    {
      fprintf (stderr,
               "read_player: flags do not match\n"
               " Expected 0x%x\n"
               " Received 0x%x\n",
               expected_flags,
               buf[4]);
      return false;
    }

  return true;
}

static bool
read_sync (VsxConnection *conn)
{
  uint8_t buf[3];
  uint8_t *large_buf = vsx_alloc(1024);

  size_t got = vsx_connection_fill_output_buffer (conn,
                                                  buf,
                                                  sizeof buf);

  bool ret = true;

  if (got != sizeof buf)
    {
      fprintf (stderr,
               "Only got %zu bytes out of %zu "
               "when trying to read the sync\n",
               got,
               sizeof buf);
      ret = false;
    }
  else if (buf[2] != VSX_PROTO_SYNC)
    {
      fprintf (stderr,
               "Expected sync command but received 0x%02x\n",
               buf[2]);
      ret = false;
    }
  else if (vsx_connection_fill_output_buffer (conn,
                                              large_buf,
                                              1024) != 0)
    {
      fprintf (stderr, "Unexpected data after sync command\n");
      ret = false;
    }

  vsx_free (large_buf);

  return ret;
}

static bool
read_connect_header (VsxConnection *conn,
                     uint64_t *person_id_out,
                     uint8_t *player_num_out)
{
  if (!read_player_id (conn, person_id_out, player_num_out))
    return false;

  if (!read_conversation_id (conn, NULL /* conversation_id_out */))
    return false;

  if (!read_n_tiles (conn, NULL /* n_tiles_out */))
    return false;

  if (!read_language_code (conn, "eo"))
    return false;

  if (!read_player_name (conn,
                         0, /* expected_player_num */
                         "Zamenhof"))
    return false;

  if (!read_player (conn,
                    0, /* expected_player_num */
                    VSX_PLAYER_CONNECTED))
    return false;

  return true;
}

static bool
check_new_player (Harness *harness,
                  const char *player_name,
                  VsxPerson **person_out)
{
  bool ret = true;
  uint64_t person_id;
  uint8_t player_num;

  if (!read_connect_header (harness->conn, &person_id, &player_num))
    {
      ret = false;
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
          ret = false;
        }
      else if (strcmp (person->player->name, player_name))
        {
          fprintf (stderr,
                   "The player name does not match:\n"
                   " Expected: %s\n"
                   " Received: %s\n",
                   player_name,
                   person->player->name);
          ret = false;
        }
      else if (person->conversation->n_players - 1 != player_num)
        {
          fprintf (stderr,
                   "New player is not last player (%i / %i)\n",
                   player_num,
                   person->conversation->n_players);
          ret = false;
        }
      else
        {
          if (person_out)
            *person_out = vsx_object_ref (person);
        }
    }

  return ret;
}

static bool
create_player (Harness *harness,
               const char *room_name,
               const char *player_name,
               VsxPerson **person_out)
{
  struct vsx_buffer buf = VSX_BUFFER_STATIC_INIT;

  vsx_buffer_append_c (&buf, 0x82);
  vsx_buffer_append_c (&buf, strlen (room_name) + strlen (player_name) + 3);
  vsx_buffer_append_c (&buf, 0x80);
  vsx_buffer_append_string (&buf, room_name);
  vsx_buffer_append_c (&buf, 0);
  vsx_buffer_append_string (&buf, player_name);
  vsx_buffer_append_c (&buf, 0);

  bool ret = true;
  struct vsx_error *error = NULL;

  if (!vsx_connection_parse_data (harness->conn,
                                  buf.data,
                                  buf.length,
                                  &error))
    {
      fprintf (stderr,
               "Unexpected error while creating new player: %s\n",
               error->message);
      vsx_error_free (error);
      ret = false;
    }
  else if (!check_new_player (harness, player_name, person_out))
    {
      ret = false;
    }

  vsx_buffer_destroy (&buf);

  return ret;
}

static bool
test_new_player (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return false;

  bool ret = create_player (harness,
                            "default:eo", "Zamenhof",
                            NULL /* person_out */);

  free_harness (harness);

  return ret;
}

static bool
reconnect_to_player (VsxConnection *conn,
                     uint64_t player_id,
                     uint16_t n_messages_received,
                     struct vsx_error **error)
{
  struct vsx_buffer buf = VSX_BUFFER_STATIC_INIT;

  player_id = VSX_UINT64_TO_LE (player_id);
  n_messages_received = VSX_UINT64_TO_LE (n_messages_received);

  vsx_buffer_append_c (&buf, 0x82);
  vsx_buffer_append_c (&buf, 1 + sizeof (uint64_t) + sizeof (uint16_t));
  vsx_buffer_append_c (&buf, 0x81);
  vsx_buffer_append (&buf, &player_id, sizeof player_id);
  vsx_buffer_append (&buf, &n_messages_received, sizeof n_messages_received);

  bool ret = true;

  if (!vsx_connection_parse_data (conn,
                                  buf.data,
                                  buf.length,
                                  error))
    {
      ret = false;
    }
  else
    {
      uint64_t person_id;

      if (!read_connect_header (conn, &person_id, NULL /* player_num */))
        {
          ret = false;
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
              ret = false;
            }
        }
    }

  vsx_buffer_destroy (&buf);

  return ret;
}

static bool
test_reconnect_ok (Harness *harness,
                   uint64_t player_id)
{
  VsxConnection *other_conn = vsx_connection_new (&harness->socket_address,
                                                  harness->conversation_set,
                                                  harness->person_set);

  bool ret = true;

  if (!negotiate_connection (other_conn))
    {
      ret = false;
    }
  else
    {
      struct vsx_error *error = NULL;

      if (!reconnect_to_player (other_conn,
                                player_id,
                                0, /* n_messages_received */
                                &error))
        {
          fprintf (stderr,
                   "test_reconnect_ok: Unexpected error: %s\n",
                   error->message);
          vsx_error_free (error);
          ret = false;
        }
    }

  vsx_connection_free (other_conn);

  return ret;
}

static bool
test_reconnect_bad_n_messages_received (Harness *harness,
                                        uint64_t player_id)
{
  VsxConnection *other_conn = vsx_connection_new (&harness->socket_address,
                                                  harness->conversation_set,
                                                  harness->person_set);

  bool ret = true;

  if (!negotiate_connection (other_conn))
    {
      ret = false;
    }
  else
    {
      struct vsx_error *error = NULL;

      if (reconnect_to_player (other_conn,
                               player_id,
                               10, /* n_messages_received */
                               &error))
        {
          fprintf (stderr,
                   "test_reconnect_bad_n_messages_received: "
                   "Reconnect unexpectedly succeeded\n");
          ret = false;
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
              ret = false;
            }

          vsx_error_free (error);
        }
    }

  vsx_connection_free (other_conn);

  return ret;
}

static bool
test_reconnect (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return false;

  VsxPerson *person;
  bool ret = true;

  if (!create_player (harness,
                      "default:eo", "Zamenhof",
                      &person))
    {
      ret = false;
    }
  else
    {
      if (!test_reconnect_ok (harness, person->hash_entry.id))
        ret = false;

      if (!test_reconnect_bad_n_messages_received (harness,
                                                   person->hash_entry.id))
        ret = false;

      vsx_object_unref (person);
    }

  free_harness (harness);

  return ret;
}

static bool
test_keep_alive (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return false;

  VsxPerson *person;
  bool ret = true;

  if (!create_player (harness,
                      "default:eo", "Zamenhof",
                      &person))
    {
      ret = false;
    }
  else
    {
      struct vsx_error *error = NULL;

      if (!vsx_connection_parse_data (harness->conn,
                                      (uint8_t *) "\x82\x1\x83",
                                      3,
                                      &error))
        {
          fprintf (stderr,
                   "test_keep_alive: Unexpected error: %s\n",
                   error->message);
          vsx_error_free (error);

          ret = false;
        }

      vsx_object_unref (person);
    }

  free_harness (harness);

  return ret;
}

static bool
read_leave_commands (VsxConnection *conn)
{
  if (!read_player (conn,
                    0, /* expected_player_num */
                    0 /* flags (no longer connected) */))
    return false;

  uint8_t buf[1 /* frame command */
             + 1 /* length */
             + 1 /* command */];

  size_t got = vsx_connection_fill_output_buffer (conn, buf, sizeof buf);

  if (got != sizeof buf)
    {
      fprintf (stderr,
               "read_leave_commands: Expected %zu bytes but received %zu\n",
               sizeof buf,
               got);
      return false;
    }

  if (buf[2] != VSX_PROTO_END)
    {
      fprintf (stderr,
               "Expected end command but received 0x%02x\n",
               buf[2]);
      return false;
    }

  if (!vsx_connection_is_finished (conn))
    {
      fprintf (stderr, "Connection is not finished after leaving\n");
      return false;
    }

  return true;
}

static bool
test_leave (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return false;

  VsxPerson *person;
  bool ret = true;

  if (!create_player (harness,
                      "default:eo", "Zamenhof",
                      &person))
    {
      ret = false;
    }
  else
    {
      struct vsx_error *error = NULL;

      if (!vsx_connection_parse_data (harness->conn,
                                      (uint8_t *) "\x82\x1\x84",
                                      3,
                                      &error))
        {
          fprintf (stderr,
                   "test_leave: Unexpected error: %s\n",
                   error->message);
          vsx_error_free (error);

          ret = false;
        }
      else if (person->conversation->n_connected_players != 0)
        {
          fprintf (stderr,
                   "test_leave: The conversation still has %i players after "
                   "leave command sent\n",
                   person->conversation->n_connected_players);
          ret = false;
        }
      else if (!read_leave_commands (harness->conn))
        ret = false;

      vsx_object_unref (person);
    }

  free_harness (harness);

  return ret;
}

static bool
read_message (VsxConnection *conn,
              int expected_player_num,
              const char *expected_message)
{
  size_t expected_message_len = strlen (expected_message);
  size_t length_length = expected_message_len >= 0x7e ? 3 : 1;

  size_t buf_size = (1 /* frame command */
                     + length_length
                     + 1 /* command */
                     + 1 /* player_num */
                     + strlen (expected_message) + 1 /* name + terminator */);
  uint8_t *buf = alloca (buf_size);

  size_t got = vsx_connection_fill_output_buffer (conn, buf, buf_size);

  if (got != buf_size)
    {
      fprintf (stderr,
               "read_message: Expected %zu bytes but received %zu\n",
               buf_size,
               got);
      return false;
    }

  uint8_t *cmd = buf + 1 + length_length;

  if (cmd[0] != VSX_PROTO_MESSAGE)
    {
      fprintf (stderr,
               "Expected message command but received 0x%02x\n",
               cmd[0]);
      return false;
    }

  if (cmd[1] != expected_player_num)
    {
      fprintf (stderr,
               "read_message: player_num does not match\n"
               " Expected: %i\n"
               " Received: %i\n",
               expected_player_num,
               cmd[1]);
      return false;
    }

  if (memcmp (cmd + 2, expected_message, strlen (expected_message) + 1))
    {
      fprintf (stderr,
               "read_message: message does not match\n"
               " Expected: %s\n"
               " Received: %.*s\n",
               expected_message,
               (int) strlen (expected_message),
               cmd + 2);
      return false;
    }

  return true;
}

static bool
check_expected_message (VsxPerson *person,
                        const char *expected_message)
{
  int n_messages =
    vsx_conversation_get_n_messages (person->conversation);

  if (n_messages < 1)
    {
      fprintf (stderr,
               "There are no messages in the conversation after sending a "
               "message\n");
      return false;
    }

  const VsxConversationMessage *message =
    vsx_conversation_get_message (person->conversation, n_messages - 1);

  if (strcmp (message->text, expected_message))
    {
      fprintf (stderr,
               "Message in conversation does not match message sent.\n"
               " Expected: %s\n"
               " Received: %s\n",
               expected_message,
               message->text);
      return false;
    }

  return true;
}

static bool
test_send_one_message (Harness *harness,
                       VsxPerson *person,
                       bool was_typing)
{
  struct vsx_error *error = NULL;
  bool ret = true;

  const char *expected_message = "Hello, world!";

  struct vsx_buffer buf = VSX_BUFFER_STATIC_INIT;

  vsx_buffer_append_c (&buf, 0x82);
  vsx_buffer_append_c (&buf, strlen (expected_message) + 2);
  vsx_buffer_append_c (&buf, 0x85);
  vsx_buffer_append_string (&buf, expected_message);
  vsx_buffer_append_c (&buf, '\0');

  if (!vsx_connection_parse_data (harness->conn,
                                  buf.data,
                                  buf.length,
                                  &error))
    {
      fprintf (stderr,
               "Unexpected error when sending message: %s\n",
               error->message);
      vsx_error_free (error);

      ret = false;
    }
  else if (!check_expected_message (person, expected_message))
    {
      ret = false;
    }
  else if (was_typing
           && !read_player (harness->conn,
                            0, /* expected_player_num */
                            VSX_PLAYER_CONNECTED))
    {
      ret = false;
    }
  else if (!read_message (harness->conn,
                          0, /* expected_player_num */
                          expected_message))
    {
      ret = false;
    }

  vsx_buffer_destroy (&buf);

  return ret;
}

static bool
test_send_fragmented_message (Harness *harness,
                              VsxPerson *person)
{
  struct vsx_error *error = NULL;
  bool ret = true;

  const char *expected_message = "Hello, fragmented world!";

  struct vsx_buffer buf = VSX_BUFFER_STATIC_INIT;

  vsx_buffer_append_c (&buf, 0x85);
  vsx_buffer_append_string (&buf, expected_message);
  vsx_buffer_append_c (&buf, '\0');

  /* Send the message as a series of one-byte fragments */
  for (int i = 0; i < buf.length; i++)
    {
      uint8_t frag[] =
        {
          i == 0 ? 0x02
          : i == buf.length - 1 ? 0x80
          : 0x00,
          1,
          buf.data[i]
        };

      if (!vsx_connection_parse_data (harness->conn,
                                      frag,
                                      sizeof frag,
                                      &error))
        {
          fprintf (stderr,
                   "Unexpected error when sending fragmented message: %s\n",
                   error->message);
          vsx_error_free (error);

          ret = false;

          goto done;
        }
    }

  if (!check_expected_message (person, expected_message))
    ret = false;
  else if (!read_message (harness->conn,
                          0, /* expected_player_num */
                          expected_message))
    ret = false;

 done:
  vsx_buffer_destroy (&buf);

  return ret;
}

static bool
test_send_long_message (Harness *harness,
                        VsxPerson *person)
{
  struct vsx_error *error = NULL;
  bool ret = true;

  struct vsx_buffer buf = VSX_BUFFER_STATIC_INIT;

  /* Send a message that is 999 ASCII characters followed by one
   * 2-byte UTF-8 character. The limit is 1000 bytes and the resulting
   * message should be clipped to remove the whole 2-byte character.
   */
  vsx_buffer_append_string (&buf, "\x82\x7e\x03\xeb\x85");

  size_t expected_start = buf.length;

  for (int i = 0; i < 997; i++)
    vsx_buffer_append_c (&buf, 'a');
  vsx_buffer_append_string (&buf, "ĥ");
  vsx_buffer_append_string (&buf, "ĉ");
  vsx_buffer_append_c (&buf, '\0');

  if (!vsx_connection_parse_data (harness->conn,
                                  buf.data, buf.length,
                                  &error))
    {
      fprintf (stderr,
               "Unexpected error when sending message: %s\n",
               error->message);
      vsx_error_free (error);

      ret = false;
    }
  else
    {
      buf.data[buf.length - 3] = '\0';

      const char *expected_message = (const char *) buf.data + expected_start;

      if (!check_expected_message (person, expected_message))
        {
          ret = false;
        }
      else if (!read_message (harness->conn,
                              0, /* expected_player_num */
                              expected_message))
        {
          ret = false;
        }
    }

  vsx_buffer_destroy (&buf);

  return ret;
}

static bool
test_send_message (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return false;

  VsxPerson *person;
  bool ret = true;

  if (!create_player (harness,
                      "default:eo", "Zamenhof",
                      &person))
    {
      ret = false;
    }
  else
    {
      if (!test_send_one_message (harness, person, false /* was_typing */))
        ret = false;

      if (!test_send_fragmented_message (harness, person))
        ret = false;

      if (!test_send_long_message (harness, person))
        ret = false;

      vsx_object_unref (person);
    }

  free_harness (harness);

  return ret;
}

static bool
test_typing_commands (Harness *harness, VsxPerson *person)
{
  static const struct
  {
    uint8_t command;
    bool typing_result;
  } typing_commands[] =
    {
      { VSX_PROTO_STOP_TYPING, false },
      { VSX_PROTO_START_TYPING, true },
      { VSX_PROTO_START_TYPING, true },
      { VSX_PROTO_STOP_TYPING, false },
      { VSX_PROTO_START_TYPING, true },
    };

  for (int i = 0; i < VSX_N_ELEMENTS (typing_commands); i++)
    {
      struct vsx_error *error = NULL;

      uint8_t buf[] = { 0x82, 0x1, typing_commands[i].command };

      if (!vsx_connection_parse_data (harness->conn,
                                      buf, sizeof buf,
                                      &error))
        {
          fprintf (stderr,
                   "test_typing_commands: %i: Unexpected error: %s\n",
                   i,
                   error->message);
          vsx_error_free (error);

          return false;
        }

      if (!!(person->player->flags & VSX_PLAYER_TYPING)
          != !!typing_commands[i].typing_result)
        {
          fprintf (stderr,
                   "test_typing_commands: %i: "
                   "Typing status is not as expected\n",
                   i);
          return false;
        }
    }

  return true;
}

static bool
test_typing (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return false;

  VsxPerson *person;
  bool ret = true;

  if (!create_player (harness,
                      "default:eo", "Zamenhof",
                      &person))
    {
      ret = false;
    }
  else
    {
      if (!test_typing_commands (harness, person))
        {
          ret = false;
        }
      else
        {
          /* Try sending a message. This should automatically set the
           * typing status to false.
           */
          if (!test_send_one_message (harness, person, true /* was_typing */))
            {
              ret = false;
            }
          else if ((person->player->flags & VSX_PLAYER_TYPING))
            {
              fprintf (stderr,
                       "Sending a message did not reset the typing status\n");
              ret = false;
            }
       }

      vsx_object_unref (person);
    }

  free_harness (harness);

  return ret;
}

static bool
read_tile (VsxConnection *conn,
           int *tile_num_out,
           int *x_out,
           int *y_out,
           int *player_out)
{
  /* The two is for the character. This would break if there were any
   * tiles that have a unicode character that requires >2 UTF-8 bytes.
   */
  uint8_t buf[1 /* frame command */
             + 1 /* length */
             + 1 /* command */
             + 1 /* tile_num */
             + 2 + 2 /* x/y */
             + 3 /* letter (max 2 bytes + terminator) */
             + 1 /* player */];

  size_t got = vsx_connection_fill_output_buffer (conn, buf, sizeof buf);

  if (got != (sizeof buf) && got != (sizeof buf) - 1)
    {
      fprintf (stderr,
               "read_tile: Expected %zu or %zu bytes but received %zu\n",
               sizeof buf,
               (sizeof buf) - 1,
               got);
      return false;
    }

  if (buf[2] != VSX_PROTO_TILE)
    {
      fprintf (stderr,
               "Expected tile command but received 0x%02x\n",
               buf[2]);
      return false;
    }

  const uint8_t *letter_start = buf + 8;
  const uint8_t *letter_end = memchr (letter_start,
                                     '\0',
                                     buf + (sizeof buf) - letter_start);

  if (letter_end == NULL)
    {
      fprintf (stderr, "Unterminated string in tile command\n");
      return false;
    }

  if (tile_num_out)
    *tile_num_out = buf[3];
  if (x_out)
    {
      int16_t val;
      memcpy (&val, buf + 4, sizeof (int16_t));
      *x_out = VSX_INT16_FROM_LE (val);
    }
  if (y_out)
    {
      int16_t val;
      memcpy (&val, buf + 6, sizeof (int16_t));
      *y_out = VSX_INT16_FROM_LE (val);
    }
  if (player_out)
    *player_out = letter_end[1];

  return true;
}

static bool
test_turn_and_move_commands (Harness *harness, VsxPerson *person)
{
  struct vsx_error *error = NULL;

  if (!vsx_connection_parse_data (harness->conn,
                                  (uint8_t *) "\x82\x1\x89",
                                  3,
                                  &error))
    {
      fprintf (stderr,
               "Unexpected error after turn command: %s\n",
               error->message);
      vsx_error_free (error);

      return false;
    }

  if (person->conversation->n_tiles_in_play != 1)
    {
      fprintf (stderr,
               "After turning a tile, n_tiles_in_play = %i\n",
               person->conversation->n_tiles_in_play);
      return false;
    }

  /* When a tile is turned the player flags will change to update the
   * current player.
   */
  if (!read_player (harness->conn,
                    0, /* expected_player_num */
                    VSX_PLAYER_CONNECTED | VSX_PLAYER_NEXT_TURN))
    return false;

  int tile_num, tile_x, tile_y, tile_player;

  if (!read_tile (harness->conn, &tile_num, &tile_x, &tile_y, &tile_player))
    return false;

  if (tile_num != 0)
    {
      fprintf (stderr,
               "Turned one tile but tile_num is %i\n",
               tile_num);
      return false;
    }

  if (tile_player != 255)
    {
      fprintf (stderr,
               "Newly turned tile has player_num %i\n",
               tile_player);
      return false;
    }

  if (!vsx_connection_parse_data (harness->conn,
                                  (uint8_t *) "\x82\x6\x88\x0\xfe\xff\x20\x00",
                                  8,
                                  &error))
    {
      fprintf (stderr,
               "Unexpected error after move command: %s\n",
               error->message);
      vsx_error_free (error);

      return false;
    }

  if (person->conversation->tiles[0].x != -2 ||
      person->conversation->tiles[0].y != 32)
    {
      fprintf (stderr,
               "After moving a tile to -2,32, it is at %i,%i\n",
               person->conversation->tiles[0].x,
               person->conversation->tiles[0].y);
      return false;
    }

  if (!read_tile (harness->conn, &tile_num, &tile_x, &tile_y, &tile_player))
    return false;

  if (tile_num != 0)
    {
      fprintf (stderr,
               "Moved first tile but tile_num is %i\n",
               tile_num);
      return false;
    }

  if (tile_player != person->player->num)
    {
      fprintf (stderr,
               "Player %i moved tile but tile command reported %i\n",
               person->player->num,
               tile_player);
      return false;
    }

  if (tile_x != -2 || tile_y != 32)
    {
      fprintf (stderr,
               "After moving a tile to -2,32, the connection reported %i,%i\n",
               tile_x,
               tile_y);
      return false;
    }

  if (vsx_connection_parse_data (harness->conn,
                                 (uint8_t *) "\x82\x6\x88\x1\x10\x00\x20\x00",
                                 8,
                                 &error))
    {
      fprintf (stderr,
               "Unexpected success after trying to move an invalid tile\n");
      return false;
    }

  const char *expected_message =
    "Player tried to move a tile that is not in play";
  bool ret = true;

  if (strcmp (error->message, expected_message))
    {
      fprintf (stderr,
               "Error message does not match after trying to move an invalid "
               "tile.\n"
               " Expected: %s\n"
               " Received: %s\n",
               expected_message,
               error->message);
      ret = false;
    }

  vsx_error_free (error);

  return ret;
}

static bool
test_turn_and_move (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return false;

  VsxPerson *person;
  bool ret = true;

  if (!create_player (harness,
                      "default:eo", "Zamenhof",
                      &person))
    {
      ret = false;
    }
  else
    {
      if (!test_turn_and_move_commands (harness, person))
        ret = false;

      vsx_object_unref (person);
    }

  free_harness (harness);

  return ret;
}

static bool
test_got_shout (Harness *harness, int shout_player)
{
  uint8_t buf[1 + 1 + 1 + 1];

  size_t got = vsx_connection_fill_output_buffer (harness->conn,
                                                  buf,
                                                  sizeof buf);

  if (got != sizeof buf)
    {
      fprintf (stderr,
               "Only got %zu bytes out of %zu "
               "when trying to read the shout\n",
               got,
               sizeof buf);
      return false;
    }

  if (buf[2] != VSX_PROTO_PLAYER_SHOUTED)
    {
      fprintf (stderr,
               "Expected PLAYER_SHOUTED command but received 0x%02x\n",
               buf[2]);
      return false;
    }

  if (buf[3] != shout_player)
    {
      fprintf (stderr,
               "test_got_shout: Expected %i but received %i\n",
               shout_player,
               buf[3]);
      return false;
    }

  return true;
}

static bool
test_shout (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return false;

  VsxPerson *person;
  bool ret = true;

  if (!create_player (harness,
                      "default:eo", "Zamenhof",
                      &person))
    {
      ret = false;
    }
  else
    {
      struct vsx_error *error = NULL;

      if (!vsx_connection_parse_data (harness->conn,
                                      (uint8_t *) "\x82\x1\x8a",
                                      3,
                                      &error))
        {
          fprintf (stderr,
                   "test_shout: Unexpected error: %s\n",
                   error->message);
          vsx_error_free (error);
          ret = false;
        }
      else if (person->conversation->last_shout_time == 0)
        {
          fprintf (stderr,
                   "test_shout: last_shout_time is still zero after shouting");
          ret = false;
        }
      else if (!test_got_shout (harness, 0 /* shout_player */))
        ret = false;

      vsx_object_unref (person);
    }

  free_harness (harness);

  return ret;
}

static bool
test_set_n_tiles (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return false;

  VsxPerson *person;
  bool ret = true;

  if (!create_player (harness,
                      "default:eo", "Zamenhof",
                      &person))
    {
      ret = false;
    }
  else
    {
      struct vsx_error *error = NULL;

      if (!vsx_connection_parse_data (harness->conn,
                                      (uint8_t *) "\x82\x2\x8b\x5",
                                      4,
                                      &error))
        {
          fprintf (stderr,
                   "test_set_n_tiles: Unexpected error: %s\n",
                   error->message);
          vsx_error_free (error);
          ret = false;
        }
      else if (person->conversation->total_n_tiles != 5)
        {
          fprintf (stderr,
                   "test_set_n_tiles: failed to set total_n_tiles "
                   "(%i != 5)\n",
                   person->conversation->total_n_tiles);
          ret = false;
        }
      else
        {
          uint8_t got_n_tiles;

          if (!read_n_tiles (harness->conn, &got_n_tiles))
            ret = false;
          else if (got_n_tiles != 5)
            {
              fprintf (stderr,
                       "test_set_n_tiles: After sending set_n_tiles 5, the "
                       "connection reported %i tiles\n",
                       got_n_tiles);
              ret = false;
            }
        }

      vsx_object_unref (person);
    }

  free_harness (harness);

  return ret;
}

static bool
test_set_language (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return false;

  VsxPerson *person;
  bool ret = true;

  if (!create_player (harness,
                      "default:eo", "Zamenhof",
                      &person))
    {
      ret = false;
    }
  else
    {
      struct vsx_error *error = NULL;

      if (!vsx_connection_parse_data (harness->conn,
                                      (uint8_t *) "\x82\x4\x8e" "en\x0",
                                      6,
                                      &error))
        {
          fprintf (stderr,
                   "test_set_language: Unexpected error: %s\n",
                   error->message);
          vsx_error_free (error);
          ret = false;
        }
      else if (strcmp(person->conversation->tile_data->language_code, "en"))
        {
          fprintf (stderr,
                   "test_set_language: failed to set tile_data "
                   "(%s != en)\n",
                   person->conversation->tile_data->language_code);
          ret = false;
        }
      else if (!read_language_code (harness->conn, "en"))
        {
          ret = false;
        }

      vsx_object_unref (person);
    }

  free_harness (harness);

  return ret;
}

static bool
test_set_unknown_language (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return false;

  VsxPerson *person;
  bool ret = true;

  if (!create_player (harness,
                      "default:eo", "Zamenhof",
                      &person))
    {
      ret = false;
    }
  else
    {
      struct vsx_error *error = NULL;

      if (!vsx_connection_parse_data (harness->conn,
                                      (uint8_t *) "\x82\x4\x8e" "zh\x0",
                                      6,
                                      &error))
        {
          fprintf (stderr,
                   "test_set_unknown_language: Unexpected error: %s\n",
                   error->message);
          vsx_error_free (error);
          ret = false;
        }
      else if (strcmp(person->conversation->tile_data->language_code, "eo"))
        {
          fprintf (stderr,
                   "test_set_unknown_language: language changed "
                   "(%s != eo)\n",
                   person->conversation->tile_data->language_code);
          ret = false;
        }
      else
        {
          /* Nothing should have changed so no LANGUAGE message should
           * be send. Instead we should get the SYNC message.
           */
          if (!read_sync (harness->conn))
            ret = false;
        }

      vsx_object_unref (person);
    }

  free_harness (harness);

  return ret;
}

static bool
test_sync (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return false;

  VsxPerson *person;
  bool ret = true;

  if (!create_player (harness,
                      "default:eo", "Zamenhof",
                      &person))
    {
      ret = false;
    }
  else
    {
      if (!read_sync (harness->conn))
        ret = false;

      vsx_object_unref (person);
    }

  free_harness (harness);

  return ret;
}

static bool
test_turn_all_tiles (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return false;

  VsxPerson *person;
  bool ret = true;

  if (!create_player (harness,
                      "default:eo", "Zamenhof",
                      &person))
    {
      ret = false;
    }
  else
    {
      for (int i = 0; i < 122; i++)
        {
          struct vsx_error *error = NULL;

          if (!vsx_connection_parse_data (harness->conn,
                                          (uint8_t *) "\x82\x1\x89",
                                          3,
                                          &error))
            {
              fprintf (stderr,
                       "Unexpected error after turn command: %s\n",
                       error->message);
              vsx_error_free (error);

              ret = false;
              break;
            }

          /* When the first and last tiles are turned the player flags
           * will change to update the current player.
           */
          if ((i == 0 || i == 121)
              && !read_player (harness->conn,
                               0, /* expected_player_num */
                               VSX_PLAYER_CONNECTED |
                               (i == 0 ? VSX_PLAYER_NEXT_TURN : 0)))
            {
              ret = false;
              break;
            }

          int tile_num;

          if (!read_tile (harness->conn, &tile_num, NULL, NULL, NULL))
            {
              ret = false;
              break;
            }

          if (tile_num != i)
            {
              fprintf (stderr,
                       "After turning tile %i, server updated tile %i\n",
                       i,
                       tile_num);
              ret = false;
              break;
            }
        }

      vsx_object_unref (person);
    }

  free_harness (harness);

  return ret;
}

static bool
test_ping_string (VsxConnection *conn,
                  const char *str)
{
  size_t str_len = strlen (str);
  size_t frame_len = str_len + 2;
  uint8_t *frame = alloca (frame_len);

  frame[0] = 0x89;
  frame[1] = str_len;

  memcpy (frame + 2, str, str_len);

  struct vsx_error *error = NULL;

  if (!vsx_connection_parse_data (conn, frame, str_len + 2, &error))
    {
      fprintf (stderr,
               "Unexpected error sending ping control frame: %s\n",
               error->message);
      vsx_error_free (error);

      return false;
    }

  /* Allocate enough space to receive the pong a second time so that
   * we can verify that the connection only sends it once.
   */
  uint8_t *result = alloca (frame_len * 2);

  size_t got = vsx_connection_fill_output_buffer (conn, result, frame_len * 2);

  if (got != frame_len)
    {
      fprintf (stderr,
               "Received %zu bytes for pong frame but %zu were expected\n",
               got,
               frame_len);
      return false;
    }

  if (result[0] != 0x8a)
    {
      fprintf (stderr,
               "Expected pong command (0x8a) but received 0x%02x)\n",
               result[0]);
      return false;
    }

  if (result[1] != str_len)
    {
      fprintf (stderr,
               "Length of pong command not as expected: %i != %zu\n",
               result[1],
               str_len);
      return false;
    }

  if (memcmp (result + 2, str, str_len))
    {
      fprintf (stderr,
               "Pong command data is different\n"
               "  Expected: %s\n"
               "  Received: %.*s\n",
               str,
               (int) str_len,
               result + 2);
      return false;
    }

  return true;
}

static bool
test_ping (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return false;

  bool ret = true;

  /* Test a simple ping */
  if (!test_ping_string (harness->conn, "poop"))
    ret = false;

  /* Test a string with the maximum control frame length */
  if (!test_ping_string (harness->conn,
                         "abcdefghijklmnopqrstuvwxyz"
                         "abcdefghijklmnopqrstuvwxyz"
                         "abcdefghijklmnopqrstuvwxyz"
                         "abcdefghijklmnopqrstuvwxyz"
                         "abcdefghijklmnopqrstu"))
    ret = false;

  free_harness (harness);

  return ret;
}

static bool
check_error_message (Harness *harness,
                     const char *command_name,
                     int command_num)
{
  uint8_t buf[1 + 1 + 1];
  size_t got = vsx_connection_fill_output_buffer (harness->conn,
                                                  buf,
                                                  sizeof buf);
  uint8_t expected[] = { 0x82, 0x01, command_num };

  if (got != sizeof buf)
    {
      fprintf (stderr,
               "Expected %s message but got %zu bytes\n",
               command_name,
               got);
      return false;
    }

  if (memcmp (buf, expected, sizeof buf))
    {
      fprintf (stderr,
               "Expected %s message. Got command 0x%02x\n",
               command_name,
               buf[2]);
      return false;
    }

  if (!vsx_connection_is_finished (harness->conn))
    {
      fprintf (stderr,
               "Connection is not finished after sending %s "
               "message\n",
               command_name);
      return false;
    }

  return true;
}

static bool
test_bad_player_id (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return false;

  bool ret = true;

  struct vsx_error *error = NULL;

  if (!vsx_connection_parse_data (harness->conn,
                                  (const uint8_t *) "\x82\xb\x81gggggggghh",
                                  13,
                                  &error))
    {
      fprintf (stderr,
               "Unexpected error after sending reconnect command: %s",
               error->message);
      vsx_error_free (error);
      ret = false;
    }
  else if (!check_error_message (harness,
                                 "bad_player_id",
                                 0x09))
    {
      ret = false;
    }

  free_harness (harness);
  return ret;
}

static bool
test_bad_conversation_id (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return false;

  bool ret = true;

  struct vsx_error *error = NULL;

  if (!vsx_connection_parse_data (harness->conn,
                                  (const uint8_t *) "\x82\xb\x8dggggggggh\0",
                                  13,
                                  &error))
    {
      fprintf (stderr,
               "Unexpected error after sending join game command: %s",
               error->message);
      vsx_error_free (error);
      ret = false;
    }
  else if (!check_error_message (harness,
                                 "bad_conversation_id",
                                 0x0b /* command num */))
    {
      ret = false;
    }

  free_harness (harness);
  return ret;
}

static bool
join_conversation_by_id (Harness *harness,
                         VsxConversation *conversation)
{
  struct vsx_buffer buf = VSX_BUFFER_STATIC_INIT;

  VsxConnection *other_conn = vsx_connection_new (&harness->socket_address,
                                                  harness->conversation_set,
                                                  harness->person_set);
  bool ret = true;

  if (!negotiate_connection (other_conn))
    {
      ret = false;
      goto out;
    }

  struct vsx_error *error = NULL;
  uint64_t conversation_id = VSX_UINT64_TO_LE (conversation->hash_entry.id);

  vsx_buffer_append_string (&buf, "\x82\x0d\x8d");
  vsx_buffer_append (&buf, &conversation_id, sizeof conversation_id);
  vsx_buffer_append_string (&buf, "Bob");
  buf.length++;

  if (!vsx_connection_parse_data (other_conn, buf.data, buf.length, &error))
    {
      fprintf (stderr,
               "Unexpected error while joining game by ID: %s\n",
               error->message);
      vsx_error_free (error);
      ret = false;
      goto out;
    }

  uint64_t new_conversation_id;

  if (!read_player_id (other_conn,
                       NULL, /* person_id_out */
                       NULL /* player_num_out */)
      || !read_conversation_id (other_conn, &new_conversation_id)
      || !read_n_tiles (other_conn, NULL /* n_tiles_out */)
      || !read_language_code (other_conn, "eo")
      || !read_player_name (other_conn,
                            0, /* expected_player_num */
                            "Zamenhof")
      || !read_player_name (other_conn,
                            1, /* expected_player_num */
                            "Bob")
      || !read_player (other_conn,
                       0, /* expected_player_num */
                       VSX_PLAYER_CONNECTED)
      || !read_player (other_conn,
                       1, /* expected_player_num */
                       VSX_PLAYER_CONNECTED))
    {
      ret = false;
      goto out;
    }

  if (new_conversation_id != conversation->hash_entry.id)
    {
      fprintf (stderr,
               "Conversation ID after joining does not match.\n"
               " Expected: %" PRIx64 "\n"
               " Received: %" PRIx64 "\n",
               conversation->hash_entry.id,
               new_conversation_id);
      ret = false;
      goto out;
    }

  VsxPlayer *player = conversation->players[conversation->n_players - 1];

  if (strcmp (player->name, "Bob"))
    {
      fprintf (stderr,
               "Name of last player after joining conversation does not match\n"
               " Expected: Bob\n"
               " Received: %s\n",
               player->name);
      ret = false;
      goto out;
  }

 out:
  vsx_buffer_destroy (&buf);
  vsx_connection_free (other_conn);
  return ret;
}

static bool
test_join_public_conversation_by_id (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return false;

  bool ret = true;

  VsxPerson *person;

  if (!create_player (harness,
                      "default:eo", "Zamenhof",
                      &person))
    {
      ret = false;
    }
  else
    {
      if (!join_conversation_by_id (harness, person->conversation))
        ret = false;

      vsx_object_unref (person);
    }

  free_harness (harness);

  return ret;
}

static bool
create_private_conversation (Harness *harness,
                             const char *language_code,
                             const char *player_name,
                             VsxPerson **person_out)
{
  struct vsx_buffer buf = VSX_BUFFER_STATIC_INIT;

  vsx_buffer_append_c (&buf, 0x82);
  vsx_buffer_append_c (&buf, strlen (language_code) + strlen (player_name) + 3);
  vsx_buffer_append_c (&buf, 0x8c);
  vsx_buffer_append_string (&buf, language_code);
  vsx_buffer_append_c (&buf, 0);
  vsx_buffer_append_string (&buf, player_name);
  vsx_buffer_append_c (&buf, 0);

  bool ret = true;
  struct vsx_error *error = NULL;

  if (!vsx_connection_parse_data (harness->conn,
                                  buf.data,
                                  buf.length,
                                  &error))
    {
      fprintf (stderr,
               "Unexpected error while creating private game: %s\n",
               error->message);
      vsx_error_free (error);
      ret = false;
    }
  else if (!check_new_player (harness, player_name, person_out))
    {
      ret = false;
    }

  vsx_buffer_destroy (&buf);

  return ret;
}

static bool
test_private_conversation (void)
{
  Harness *harness = create_negotiated_harness ();

  if (harness == NULL)
    return false;

  bool ret = true;

  VsxPerson *person;

  if (!create_private_conversation (harness,
                                    "eo",
                                    "Zamenhof",
                                    &person))
    {
      ret = false;
    }
  else
    {
      if (!join_conversation_by_id (harness, person->conversation))
        ret = false;

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

  if (!test_set_language ())
    ret = EXIT_FAILURE;

  if (!test_set_unknown_language ())
    ret = EXIT_FAILURE;

  if (!test_sync ())
    ret = EXIT_FAILURE;

  if (!test_turn_all_tiles ())
    ret = EXIT_FAILURE;

  if (!test_ping ())
    ret = EXIT_FAILURE;

  if (!test_bad_player_id ())
    ret = EXIT_FAILURE;

  if (!test_bad_conversation_id ())
    ret = EXIT_FAILURE;

  if (!test_join_public_conversation_by_id ())
    ret = EXIT_FAILURE;

  if (!test_private_conversation ())
    ret = EXIT_FAILURE;

  vsx_main_context_free (vsx_main_context_get_default (NULL /* error */));

  return ret;
}
