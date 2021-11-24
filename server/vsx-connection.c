/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2013, 2015, 2020, 2021  Neil Roberts
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

#include "vsx-connection.h"

#include <inttypes.h>

#include "vsx-ws-parser.h"
#include "vsx-proto.h"

/* VsxConnection specifically handles connections using the WebSocket
 * protocol. Connections via the HTTP protocol use an HTTP parser
 * instead. The weird name is because eventually the HTTP part is
 * expected to be removed and VsxConnection will be the only type of
 * connection.
 */

typedef enum
{
  VSX_CONNECTION_STATE_READING_WS_HEADERS,
  VSX_CONNECTION_STATE_WRITING_WS_RESPONSE,
  VSX_CONNECTION_STATE_WRITING_DATA,
  VSX_CONNECTION_STATE_DONE,
} VsxConnectionState;

struct _VsxConnection
{
  VsxConnectionState state;

  GSocketAddress *socket_address;
  VsxConversationSet *conversation_set;
  VsxPersonSet *person_set;

  /* This is freed and becomes NULL once the headers have all
   * been parsed.
   */
  VsxWsParser *ws_parser;

  guint8 read_buf[1024];
  size_t read_buf_pos;

  /* If pong_queued is non-zero then pong_data then we need to
   * send a pong control frame with the payload given payload.
   */
  gboolean pong_queued;
  _Static_assert (VSX_PROTO_MAX_CONTROL_FRAME_PAYLOAD <= G_MAXUINT8,
                  "The max pong data length is too for a uint8_t");
  guint8 pong_data_length;
  guint8 pong_data[VSX_PROTO_MAX_CONTROL_FRAME_PAYLOAD];

  /* If message_data_length is non-zero then we are part way
   * through reading a message whose payload is stored in
   * message_data.
   */
  _Static_assert (VSX_PROTO_MAX_PAYLOAD_SIZE <= G_MAXUINT16,
                  "The message size is too long for a uint16_t");
  guint16 message_data_length;
  guint8 message_data[VSX_PROTO_MAX_PAYLOAD_SIZE];
};

static const char
ws_header_prefix[] =
  "HTTP/1.1 101 Switching Protocols\r\n"
  "Upgrade: websocket\r\n"
  "Connection: Upgrade\r\n"
  "Sec-WebSocket-Accept: ";

static const char
ws_header_postfix[] = "\r\n\r\n";

static gboolean
handle_new_player(VsxConnection *conn,
                  GError **error)
{
  return TRUE;
}

static gboolean
process_message (VsxConnection *conn,
                 GError **error)
{
  if (conn->message_data_length < 1)
    {
      g_set_error (error,
                   VSX_CONNECTION_ERROR,
                   VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                   "Client sent an empty message");
      return FALSE;
    }

  switch (conn->message_data[0])
    {
    case VSX_PROTO_NEW_PLAYER:
      return handle_new_player (conn, error);
    }

  g_set_error (error,
               VSX_CONNECTION_ERROR,
               VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
               "Client sent an unknown message ID (0x%x)",
               conn->message_data[0]);

  return FALSE;
}

VsxConnection *
vsx_connection_new (GSocketAddress *socket_address,
                    VsxConversationSet *conversation_set,
                    VsxPersonSet *person_set)
{
  VsxConnection *conn = g_new0 (VsxConnection, 1);

  conn->socket_address = g_object_ref (socket_address);
  conn->conversation_set = vsx_object_ref (conversation_set);
  conn->person_set = vsx_object_ref (person_set);

  conn->ws_parser = vsx_ws_parser_new ();

  return conn;
}

static int
write_ws_response (VsxConnection *conn,
                   guint8 *buffer,
                   size_t buffer_size)
{
  size_t key_hash_size;
  const guint8 *key_hash = vsx_ws_parser_get_key_hash (conn->ws_parser,
                                                       &key_hash_size);

  /* Magic formula taken from the docs for g_base64_encode_step */
  size_t base64_size_needed = (key_hash_size / 3 + 1) * 4 + 4;

  if (base64_size_needed
      + (sizeof ws_header_prefix) - 1
      + (sizeof ws_header_postfix) - 1
      > buffer_size)
    {
      /* This probably shouldn’t happen because the WS response should
       * be the first thing we write which means the buffer should be
       * empty.
       */
      return -1;
    }

  guint8 *p = buffer;

  memcpy (p, ws_header_prefix, (sizeof ws_header_prefix) - 1);
  p += (sizeof ws_header_prefix) - 1;

  int state = 0, save = 0;

  p += g_base64_encode_step (key_hash,
                             key_hash_size,
                             FALSE, /* break_lines */
                             (char *) p,
                             &state,
                             &save);
  p += g_base64_encode_close (FALSE, /* break_lines */
                              (char *) p,
                              &state,
                              &save);

  memcpy (p, ws_header_postfix, (sizeof ws_header_postfix) - 1);
  p += (sizeof ws_header_postfix) - 1;

  return p - buffer;
}

size_t
vsx_connection_fill_output_buffer (VsxConnection *conn,
                                   guint8 *buffer,
                                   size_t buffer_size)
{
  size_t total_wrote = 0;
  int wrote;

  while (TRUE)
    {
      switch (conn->state)
        {
        case VSX_CONNECTION_STATE_READING_WS_HEADERS:
          return total_wrote;

        case VSX_CONNECTION_STATE_WRITING_WS_RESPONSE:
          wrote = write_ws_response(conn,
                                    buffer + total_wrote,
                                    buffer_size - total_wrote);
          if (wrote == -1)
            return total_wrote;

          total_wrote += wrote;

          conn->state = VSX_CONNECTION_STATE_WRITING_DATA;

          break;

        case VSX_CONNECTION_STATE_WRITING_DATA:
          /* FIXME */
          return total_wrote;

        case VSX_CONNECTION_STATE_DONE:
          return total_wrote;
        }
    }
}

gboolean
vsx_connection_parse_eof (VsxConnection *conn,
                          GError **error)
{
  if (conn->state == VSX_CONNECTION_STATE_READING_WS_HEADERS)
    {
      g_set_error (error,
                   VSX_CONNECTION_ERROR,
                   VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                   "Client closed the connection before finishing WebSocket "
                   "negotiation");
      return FALSE;
    }

  if (conn->read_buf_pos > 0)
    {
      g_set_error (error,
                   VSX_CONNECTION_ERROR,
                   VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                   "Client closed the connection in the middle of a frame");
      return FALSE;
    }

  conn->state = VSX_CONNECTION_STATE_DONE;

  return TRUE;
}

static gboolean
process_control_frame (VsxConnection *conn,
                       int opcode,
                       const guint8 *data,
                       size_t data_length,
                       GError **error)
{
  switch (opcode)
    {
    case 0x8:
      /* Close control frame, ignore */
      return TRUE;
    case 0x9:
      g_assert (data_length < sizeof conn->pong_data);
      memcpy (conn->pong_data, data, data_length);
      conn->pong_data_length = data_length;
      conn->pong_queued = TRUE;
      break;
    case 0xa:
      /* pong, ignore */
      break;
    default:
      g_set_error (error,
                   VSX_CONNECTION_ERROR,
                   VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                   "Client sent an unknown control frame");
    return FALSE;
  }

  return TRUE;
}

static void
unmask_data (guint32 mask,
             guint8 *buffer,
             size_t buffer_length)
{
  int i;

  for (i = 0; i + sizeof mask <= buffer_length; i += sizeof mask)
    {
      guint32 val;

      memcpy (&val, buffer + i, sizeof val);
      val ^= mask;
      memcpy (buffer + i, &val, sizeof val);
    }

  for (; i < buffer_length; i++)
    buffer[i] ^= ((guint8 *) &mask)[i % 4];
}

static gboolean
process_frames (VsxConnection *conn,
                GError **error)
{
  guint8 *data = conn->read_buf;
  size_t length = conn->read_buf_pos;
  gboolean has_mask;
  gboolean is_fin;
  guint32 mask;
  guint64 payload_length;
  guint8 opcode;

  while (TRUE)
    {
      if (length < 2)
        break;

      int header_size = 2;

      is_fin = data[0] & 0x80;
      opcode = data[0] & 0xf;
      has_mask = data[1] & 0x80;

      payload_length = data[1] & 0x7f;

      if (payload_length == 126)
        {
          guint16 word;
          if (length < header_size + sizeof word)
            break;
          memcpy (&word, data + header_size, sizeof word);
          payload_length = GUINT16_FROM_BE (word);
          header_size += sizeof word;
        }
      else if (payload_length == 127)
        {
          if (length < header_size + sizeof payload_length)
            break;
          memcpy (&payload_length,
                  data + header_size,
                  sizeof payload_length);
          payload_length = GUINT64_FROM_BE (payload_length);
          header_size += sizeof payload_length;
        }

      if (has_mask)
        header_size += sizeof mask;

      /* RSV bits must be zero */
      if (data[0] & 0x70)
        {
          g_set_error (error,
                       VSX_CONNECTION_ERROR,
                       VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                       "Client sent a frame with non-zero RSV bits");
          return FALSE;
        }

      if (opcode & 0x8)
        {
          /* Control frame */
          if (payload_length > VSX_PROTO_MAX_CONTROL_FRAME_PAYLOAD)
            {
              g_set_error (error,
                           VSX_CONNECTION_ERROR,
                           VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                           "Client sent a control frame (0x%x) "
                           "that is too long (%" PRIu64 ")",
                           opcode,
                           payload_length);
              return FALSE;
            }
          if (!is_fin)
            {
              g_set_error (error,
                           VSX_CONNECTION_ERROR,
                           VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                           "Client sent a fragmented "
                           "control frame");
              return FALSE;
            }
        }
      else if (opcode == 0x2 || opcode == 0x0)
        {
          if (payload_length + conn->message_data_length
              > VSX_PROTO_MAX_PAYLOAD_SIZE)
            {
              g_set_error (error,
                           VSX_CONNECTION_ERROR,
                           VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                           "Client sent a message (0x%x) "
                           "that is too long (%" PRIu64 ")",
                           opcode,
                           payload_length);
              return FALSE;
            }
          if (opcode == 0x0 && conn->message_data_length == 0)
            {
              g_set_error (error,
                           VSX_CONNECTION_ERROR,
                           VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                           "Client sent a continuation frame "
                           "without starting a message");
              return FALSE;
            }
          if (payload_length == 0 && !is_fin)
            {
              g_set_error (error,
                           VSX_CONNECTION_ERROR,
                           VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                           "Client sent an empty fragmented "
                           "message");
              return FALSE;
            }
        }
      else
        {
          g_set_error (error,
                       VSX_CONNECTION_ERROR,
                       VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                       "Client sent a frame opcode (0x%x) which "
                       "the server doesn’t understand",
                       opcode);
          return FALSE;
        }

      if (payload_length + header_size > length)
        break;

      data += header_size;
      length -= header_size;

      if (has_mask)
        {
          memcpy (&mask, data - sizeof mask, sizeof mask);
          unmask_data (mask, data, payload_length);
        }

      if (opcode & 0x8)
        {
          if (!process_control_frame (conn,
                                      opcode,
                                      data,
                                      payload_length,
                                      error))
            return FALSE;
        }
      else
        {
          memcpy (conn->message_data + conn->message_data_length,
                  data,
                  payload_length);
          conn->message_data_length += payload_length;

          if (is_fin)
            {
              if (!process_message (conn, error))
                return FALSE;

              conn->message_data_length = 0;
            }
        }

      data += payload_length;
      length -= payload_length;
    }

  memmove (conn->read_buf, data, length);
  conn->read_buf_pos = length;

  return TRUE;
}

gboolean
vsx_connection_parse_data (VsxConnection *conn,
                           const guint8 *buffer,
                           size_t buffer_length,
                           GError **error)
{
  if (conn->state == VSX_CONNECTION_STATE_READING_WS_HEADERS)
    {
      size_t consumed;

      switch (vsx_ws_parser_parse_data (conn->ws_parser,
                                        buffer,
                                        buffer_length,
                                        &consumed,
                                        error))
        {
        case VSX_WS_PARSER_RESULT_NEED_MORE_DATA:
          return TRUE;
        case VSX_WS_PARSER_RESULT_ERROR:
          return FALSE;
        case VSX_WS_PARSER_RESULT_FINISHED:
          conn->state = VSX_CONNECTION_STATE_WRITING_WS_RESPONSE;
          buffer += consumed;
          buffer_length -= consumed;
          break;
        }
    }

  while (buffer_length > 0)
    {
      size_t to_copy = MIN (buffer_length,
                            (sizeof conn->read_buf) - conn->read_buf_pos);
      memcpy (conn->read_buf + conn->read_buf_pos, buffer, to_copy);
      conn->read_buf_pos += to_copy;
      buffer_length -= to_copy;
      buffer += to_copy;

      if (!process_frames (conn, error))
        return FALSE;
    }

  return TRUE;
}

gboolean
vsx_connection_is_finished (VsxConnection *conn)
{
  return conn->state == VSX_CONNECTION_STATE_DONE;
}

gboolean
vsx_connection_has_data (VsxConnection *conn)
{
  switch (conn->state)
    {
    case VSX_CONNECTION_STATE_READING_WS_HEADERS:
      return FALSE;
    case VSX_CONNECTION_STATE_WRITING_WS_RESPONSE:
      return TRUE;
    case VSX_CONNECTION_STATE_WRITING_DATA:
      /* FIXME */
      return FALSE;
    case VSX_CONNECTION_STATE_DONE:
      return FALSE;
    }

  g_warn_if_reached ();

  return FALSE;
}

void
vsx_connection_free (VsxConnection *conn)
{
  g_object_unref (conn->socket_address);
  vsx_object_unref (conn->conversation_set);
  vsx_object_unref (conn->person_set);

  if (conn->ws_parser)
    vsx_ws_parser_free (conn->ws_parser);

  g_free (conn);
}

GQuark
vsx_connection_error_quark (void)
{
  return g_quark_from_static_string ("vsx-connection-error");
}
