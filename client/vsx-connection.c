/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2012, 2013  Neil Roberts
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

#include "vsx-connection.h"

#include <string.h>
#include <stdarg.h>
#include <gio/gio.h>
#include <assert.h>
#include <stdalign.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>

#include "vsx-player-private.h"
#include "vsx-tile-private.h"
#include "vsx-proto.h"
#include "vsx-list.h"
#include "vsx-util.h"
#include "vsx-buffer.h"
#include "vsx-slab.h"
#include "vsx-utf8.h"
#include "vsx-netaddress.h"
#include "vsx-socket.h"
#include "vsx-error.h"

static const uint8_t
ws_terminator[] = "\r\n\r\n";

#define WS_TERMINATOR_LENGTH ((sizeof ws_terminator) - 1)

typedef enum
{
  VSX_CONNECTION_DIRTY_FLAG_WS_HEADER = (1 << 0),
  VSX_CONNECTION_DIRTY_FLAG_HEADER = (1 << 1),
  VSX_CONNECTION_DIRTY_FLAG_KEEP_ALIVE = (1 << 2),
  VSX_CONNECTION_DIRTY_FLAG_LEAVE = (1 << 3),
  VSX_CONNECTION_DIRTY_FLAG_SHOUT = (1 << 4),
  VSX_CONNECTION_DIRTY_FLAG_TURN = (1 << 5),
} VsxConnectionDirtyFlag;

typedef int (* VsxConnectionWriteStateFunc) (VsxConnection *conn,
                                             uint8_t *buffer,
                                             size_t buffer_size);

static void
vsx_connection_queue_reconnect (VsxConnection *connection);

static void
update_poll (VsxConnection *connection);

/* Initial timeout (in seconds) before attempting to reconnect after
   an error. The timeout will be doubled every time there is a
   failure */
#define VSX_CONNECTION_INITIAL_TIMEOUT 16

/* If the timeout reaches this maximum then it won't be doubled further */
#define VSX_CONNECTION_MAX_TIMEOUT 512

/* Time in seconds after the last message before sending a keep alive
   message (2.5 minutes) */
#define VSX_CONNECTION_KEEP_ALIVE_TIME 150

typedef enum
{
  VSX_CONNECTION_RUNNING_STATE_DISCONNECTED,
  /* connect has been called and we are waiting for it to
   * become ready for writing */
  VSX_CONNECTION_RUNNING_STATE_RECONNECTING,
  VSX_CONNECTION_RUNNING_STATE_RUNNING,
  VSX_CONNECTION_RUNNING_STATE_WAITING_FOR_RECONNECT
} VsxConnectionRunningState;

typedef struct
{
  int num;
  int x;
  int y;
  VsxList link;
} VsxConnectionTileToMove;

typedef struct
{
  VsxList link;
  /* Over-allocated */
  char message[1];
} VsxConnectionMessageToSend;

struct _VsxConnection
{
  GSocketAddress *address;
  char *room;
  char *player_name;
  guint reconnect_timeout;
  guint reconnect_handler;
  VsxPlayer *self;
  bool has_person_id;
  uint64_t person_id;
  VsxConnectionRunningState running_state;
  VsxConnectionState state;
  bool typing;
  bool sent_typing_state;
  bool write_finished;
  int next_message_num;

  VsxSignal event_signal;

  VsxConnectionDirtyFlag dirty_flags;
  VsxList tiles_to_move;
  VsxList messages_to_send;

  int sock;
  GIOChannel *sock_channel;
  guint sock_source;
  /* The condition that the source was last created with so we can
   * know if we need to recreate it.
   */
  GIOCondition sock_condition;

  /* Array of pointers to players, indexed by player num. This can
   * have NULL gaps. */
  struct vsx_buffer players;

  /* Slab allocator for VsxTile */
  struct vsx_slab_allocator tile_allocator;
  /* Array of pointers to tiles, indexed by tile num. This can have
   * NULL gaps. */
  struct vsx_buffer tiles;

  /* A timeout for sending a keep alive message */
  guint keep_alive_timeout;
  GTimer *keep_alive_time;

  unsigned int output_length;
  uint8_t output_buffer[VSX_PROTO_MAX_PAYLOAD_SIZE
                        + VSX_PROTO_MAX_FRAME_HEADER_LENGTH];

  unsigned int input_length;
  uint8_t input_buffer[VSX_PROTO_MAX_PAYLOAD_SIZE
                       + VSX_PROTO_MAX_FRAME_HEADER_LENGTH];

  /* Position within ws terminator that we have found so far. If this
   * is greater than the terminator length then we’ve finished the
   * WebSocket negotation.
   */
  unsigned int ws_terminator_pos;
};

static gboolean
vsx_connection_keep_alive_cb (void *data)
{
  VsxConnection *connection = data;

  connection->keep_alive_timeout = 0;

  connection->dirty_flags |= VSX_CONNECTION_DIRTY_FLAG_KEEP_ALIVE;

  update_poll (connection);

  return false;
}

static void
vsx_connection_queue_keep_alive (VsxConnection *connection)
{
  if (connection->keep_alive_timeout)
    g_source_remove (connection->keep_alive_timeout);

  connection->keep_alive_timeout
    = g_timeout_add_seconds (VSX_CONNECTION_KEEP_ALIVE_TIME + 1,
                             vsx_connection_keep_alive_cb,
                             connection);

  g_timer_start (connection->keep_alive_time);
}

static void
vsx_connection_signal_error (VsxConnection *connection,
                             GError *error)
{
  VsxConnectionEvent event =
    {
      .type = VSX_CONNECTION_EVENT_TYPE_ERROR,
      .error = { .error = error },
    };

  vsx_signal_emit (&connection->event_signal, &event);
}

static void
close_socket (VsxConnection *connection)
{
  if (connection->keep_alive_timeout)
    {
      g_source_remove (connection->keep_alive_timeout);
      connection->keep_alive_timeout = 0;
    }

  if (connection->sock_source)
    {
      g_source_remove (connection->sock_source);
      connection->sock_source = 0;
      connection->sock_condition = 0;
    }

  if (connection->sock_channel)
    {
      g_io_channel_unref (connection->sock_channel);
      connection->sock_channel = NULL;
    }

  if (connection->sock != -1)
    {
      vsx_close (connection->sock);
      connection->sock = -1;
    }
}

void
vsx_connection_set_typing (VsxConnection *connection,
                           bool typing)
{
  if (connection->typing != typing)
    {
      connection->typing = typing;
      update_poll (connection);
    }
}

void
vsx_connection_shout (VsxConnection *connection)
{
  connection->dirty_flags |= VSX_CONNECTION_DIRTY_FLAG_SHOUT;

  update_poll (connection);
}

void
vsx_connection_turn (VsxConnection *connection)
{
  connection->dirty_flags |= VSX_CONNECTION_DIRTY_FLAG_TURN;

  update_poll (connection);
}

void
vsx_connection_move_tile (VsxConnection *connection,
                          int tile_num,
                          int x,
                          int y)
{
  VsxConnectionTileToMove *tile;

  vsx_list_for_each (tile, &connection->tiles_to_move, link)
    {
      if (tile->num == tile_num)
        goto found_tile;
    }

  tile = g_new (VsxConnectionTileToMove, 1);
  tile->num = tile_num;
  vsx_list_insert (connection->tiles_to_move.prev, &tile->link);

 found_tile:
  tile->x = x;
  tile->y = y;

  update_poll (connection);
}

static void
vsx_connection_set_state (VsxConnection *connection,
                          VsxConnectionState state)
{
  if (connection->state == state)
    return;

  connection->state = state;

  VsxConnectionEvent event =
    {
      .type = VSX_CONNECTION_EVENT_TYPE_STATE_CHANGED,
      .state_changed = { .state = state },
    };

  vsx_signal_emit (&connection->event_signal, &event);
}

static void *
get_pointer_from_buffer (struct vsx_buffer *buf,
                         int num)
{
  size_t n_entries = buf->length / sizeof (void *);

  if (num >= n_entries)
    return NULL;

  return ((void **) buf->data)[num];
}

static void
set_pointer_in_buffer (struct vsx_buffer *buf,
                       int num,
                       void *value)
{
  size_t n_entries = buf->length / sizeof (void *);

  if (num >= n_entries)
    {
      size_t old_length = buf->length;
      vsx_buffer_set_length (buf, (num + 1) * sizeof (void *));
      memset (buf->data + old_length, 0, num * sizeof (void *) - old_length);
    }

  ((void **) buf->data)[num] = value;
}

static VsxPlayer *
get_or_create_player (VsxConnection *connection,
                      int player_num)
{
  VsxPlayer *player =
    get_pointer_from_buffer (&connection->players, player_num);

  if (player == NULL)
    {
      player = vsx_calloc (sizeof *player);
      player->num = player_num;
      set_pointer_in_buffer (&connection->players, player_num, player);
    }

  return player;
}

static bool
handle_player_id (VsxConnection *connection,
                  const uint8_t *payload,
                  size_t payload_length,
                  GError **error)
{
  uint8_t self_num;

  if (!vsx_proto_read_payload (payload + 1,
                               payload_length - 1,

                               VSX_PROTO_TYPE_UINT64,
                               &connection->person_id,

                               VSX_PROTO_TYPE_UINT8,
                               &self_num,

                               VSX_PROTO_TYPE_NONE))
    {
      g_set_error (error,
                   VSX_CONNECTION_ERROR,
                   VSX_CONNECTION_ERROR_BAD_DATA,
                   "The server sent an invalid player_id command");
      return false;
    }

  connection->self = get_or_create_player (connection, self_num);

  connection->has_person_id = true;

  if (connection->state == VSX_CONNECTION_STATE_AWAITING_HEADER)
    vsx_connection_set_state (connection, VSX_CONNECTION_STATE_IN_PROGRESS);

  return true;
}

static bool
handle_message (VsxConnection *connection,
                const uint8_t *payload,
                size_t payload_length,
                GError **error)
{
  uint8_t person;
  const char *text;

  if (!vsx_proto_read_payload (payload + 1,
                               payload_length - 1,

                               VSX_PROTO_TYPE_UINT8,
                               &person,

                               VSX_PROTO_TYPE_STRING,
                               &text,

                               VSX_PROTO_TYPE_NONE))
    {
      g_set_error (error,
                   VSX_CONNECTION_ERROR,
                   VSX_CONNECTION_ERROR_BAD_DATA,
                   "The server sent an invalid message command");
      return false;
    }

  connection->next_message_num++;

  VsxConnectionEvent event =
    {
      .type = VSX_CONNECTION_EVENT_TYPE_MESSAGE,
      .message =
      {
        .player = get_or_create_player (connection, person),
        .message = text,
      },
    };

  vsx_signal_emit (&connection->event_signal, &event);

  return true;
}

static bool
handle_tile (VsxConnection *connection,
             const uint8_t *payload,
             size_t payload_length,
             GError **error)
{
  uint8_t num, player;
  int16_t x, y;
  const char *letter;

  if (!vsx_proto_read_payload (payload + 1,
                               payload_length - 1,

                               VSX_PROTO_TYPE_UINT8,
                               &num,

                               VSX_PROTO_TYPE_INT16,
                               &x,

                               VSX_PROTO_TYPE_INT16,
                               &y,

                               VSX_PROTO_TYPE_STRING,
                               &letter,

                               VSX_PROTO_TYPE_UINT8,
                               &player,

                               VSX_PROTO_TYPE_NONE)
      || *letter == 0
      || *vsx_utf8_next (letter) != 0)
    {
      g_set_error (error,
                   VSX_CONNECTION_ERROR,
                   VSX_CONNECTION_ERROR_BAD_DATA,
                   "The server sent an invalid tile command");
      return false;
    }

  bool is_new = false;

  VsxTile *tile = get_pointer_from_buffer (&connection->tiles, num);

  if (tile == NULL)
    {
      tile = vsx_slab_allocate (&connection->tile_allocator,
                                sizeof *tile,
                                alignof *tile);

      tile->num = num;

      set_pointer_in_buffer (&connection->tiles, num, tile);

      is_new = true;
    }

  tile->x = x;
  tile->y = y;
  tile->letter = vsx_utf8_get_char (letter);

  VsxConnectionEvent event =
    {
      .type = VSX_CONNECTION_EVENT_TYPE_TILE_CHANGED,
      .tile_changed =
      {
        .new_tile = is_new,
        .tile = tile,
      },
    };

  vsx_signal_emit (&connection->event_signal, &event);

  return true;
}

static void
emit_player_changed (VsxConnection *connection,
                     VsxPlayer *player)
{
  VsxConnectionEvent event =
    {
      .type = VSX_CONNECTION_EVENT_TYPE_PLAYER_CHANGED,
      .player_changed = { .player = player },
    };

  vsx_signal_emit (&connection->event_signal, &event);
}

static bool
handle_player_name (VsxConnection *connection,
                    const uint8_t *payload,
                    size_t payload_length,
                    GError **error)
{
  uint8_t num;
  const char *name;

  if (!vsx_proto_read_payload (payload + 1,
                               payload_length - 1,

                               VSX_PROTO_TYPE_UINT8,
                               &num,

                               VSX_PROTO_TYPE_STRING,
                               &name,

                               VSX_PROTO_TYPE_NONE))
    {
      g_set_error (error,
                   VSX_CONNECTION_ERROR,
                   VSX_CONNECTION_ERROR_BAD_DATA,
                   "The server sent an invalid player_name command");
      return false;
    }

  VsxPlayer *player = get_or_create_player (connection, num);

  g_free (player->name);
  player->name = g_strdup (name);

  emit_player_changed (connection, player);

  return true;
}

static bool
handle_player (VsxConnection *connection,
               const uint8_t *payload,
               size_t payload_length,
               GError **error)
{
  uint8_t num, flags;

  if (!vsx_proto_read_payload (payload + 1,
                               payload_length - 1,

                               VSX_PROTO_TYPE_UINT8,
                               &num,

                               VSX_PROTO_TYPE_UINT8,
                               &flags,

                               VSX_PROTO_TYPE_NONE))
    {
      g_set_error (error,
                   VSX_CONNECTION_ERROR,
                   VSX_CONNECTION_ERROR_BAD_DATA,
                   "The server sent an invalid player command");
      return false;
    }

  VsxPlayer *player = get_or_create_player (connection, num);

  player->flags = flags;

  emit_player_changed (connection, player);

  return true;
}

static bool
handle_player_shouted (VsxConnection *connection,
                       const uint8_t *payload,
                       size_t payload_length,
                       GError **error)
{
  uint8_t player_num;

  if (!vsx_proto_read_payload (payload + 1,
                               payload_length - 1,

                               VSX_PROTO_TYPE_UINT8,
                               &player_num,

                               VSX_PROTO_TYPE_NONE))
    {
      g_set_error (error,
                   VSX_CONNECTION_ERROR,
                   VSX_CONNECTION_ERROR_BAD_DATA,
                   "The server sent an invalid player_shouted command");
      return false;
    }

  VsxConnectionEvent event =
    {
      .type = VSX_CONNECTION_EVENT_TYPE_PLAYER_SHOUTED,
      .player_shouted =
      {
        .player = get_or_create_player (connection, player_num)
      },
    };

  vsx_signal_emit (&connection->event_signal, &event);

  return true;
}

static bool
handle_end (VsxConnection *connection,
            const uint8_t *payload,
            size_t payload_length,
            GError **error)
{
  if (!vsx_proto_read_payload (payload + 1,
                               payload_length - 1,

                               VSX_PROTO_TYPE_NONE))
    {
      g_set_error (error,
                   VSX_CONNECTION_ERROR,
                   VSX_CONNECTION_ERROR_BAD_DATA,
                   "The server sent an invalid end command");
      return false;
    }

  vsx_connection_set_state (connection, VSX_CONNECTION_STATE_DONE);

  return true;
}

static bool
process_message (VsxConnection *connection,
                 const uint8_t *payload,
                 size_t payload_length,
                 GError **error)
{
  if (payload_length < 1)
    {
      g_set_error (error,
                   VSX_CONNECTION_ERROR,
                   VSX_CONNECTION_ERROR_BAD_DATA,
                   "The server sent an empty message");
      return false;
    }

  switch (payload[0])
    {
    case VSX_PROTO_PLAYER_ID:
      return handle_player_id (connection, payload, payload_length, error);
    case VSX_PROTO_MESSAGE:
      return handle_message (connection, payload, payload_length, error);
    case VSX_PROTO_TILE:
      return handle_tile (connection, payload, payload_length, error);
    case VSX_PROTO_PLAYER_NAME:
      return handle_player_name (connection, payload, payload_length, error);
    case VSX_PROTO_PLAYER:
      return handle_player (connection, payload, payload_length, error);
    case VSX_PROTO_PLAYER_SHOUTED:
      return handle_player_shouted (connection, payload, payload_length, error);
    case VSX_PROTO_END:
      return handle_end (connection, payload, payload_length, error);
    }

  return true;
}

static void
report_error (VsxConnection *connection,
              GError *error)
{
  close_socket (connection);
  vsx_connection_queue_reconnect (connection);
  vsx_connection_signal_error (connection, error);
}

static bool
get_payload_length (const uint8_t *buf,
                    size_t buf_length,
                    size_t *payload_length_out,
                    const uint8_t **payload_start_out)
{
  if (buf_length < 2)
    return false;

  switch (buf[1])
    {
    case 0x7e:
      if (buf_length < 4)
        return false;

      *payload_length_out = vsx_proto_read_uint16_t (buf + 2);
      *payload_start_out = buf + 4;
      return true;

    case 0x7f:
      if (buf_length < 6)
        return false;

      *payload_length_out = vsx_proto_read_uint32_t (buf + 2);
      *payload_start_out = buf + 6;
      return true;

    default:
      *payload_length_out = buf[1];
      *payload_start_out = buf + 2;
      return true;
    }
}

static bool
is_would_block_error (int err)
{
  return err == EAGAIN || err == EWOULDBLOCK;
}

static void
handle_read (VsxConnection *connection)
{
  GError *error = NULL;

  ssize_t got = read (connection->sock,
                      connection->input_buffer
                      + connection->input_length,
                      (sizeof connection->input_buffer)
                      - connection->input_length);
  if (got == -1)
    {
      if (!is_would_block_error (errno) && errno != EINTR)
        {
          GError *error = NULL;

          g_set_error (&error,
                       G_FILE_ERROR,
                       g_file_error_from_errno (errno),
                       "Error reading from socket: %s",
                       strerror (errno));

          report_error (connection, error);

          g_clear_error (&error);
        }
    }
  else if (got == 0)
    {
      if (connection->state == VSX_CONNECTION_STATE_DONE)
        {
          vsx_connection_set_running (connection, false);
        }
      else
        {
          error = g_error_new (VSX_CONNECTION_ERROR,
                               VSX_CONNECTION_ERROR_CONNECTION_CLOSED,
                               "The server unexpectedly closed the connection");
          report_error (connection, error);
          g_error_free (error);
        }
    }
  else
    {
      const uint8_t *p = connection->input_buffer;

      connection->input_length += got;

      if (connection->ws_terminator_pos < WS_TERMINATOR_LENGTH)
        {
          while (p - connection->input_buffer < connection->input_length)
            {
              if (*(p++) == ws_terminator[connection->ws_terminator_pos])
                {
                  if (++connection->ws_terminator_pos >= WS_TERMINATOR_LENGTH)
                    goto terminated;
                }
              else
                {
                  connection->ws_terminator_pos = 0;
                }
            }

          /* If we make it here then we haven’t found the end of the
           * terminator yet.
           */
          connection->input_length = 0;
          return;
        }

    terminated: ((void) 0);

      size_t payload_length;
      const uint8_t *payload_start;

      while (get_payload_length (p,
                                 connection->input_buffer
                                 + connection->input_length
                                 - p,
                                 &payload_length,
                                 &payload_start))
        {
          if (payload_length > VSX_PROTO_MAX_PAYLOAD_SIZE)
            {
              error = g_error_new (VSX_CONNECTION_ERROR,
                                   VSX_CONNECTION_ERROR_CONNECTION_CLOSED,
                                   "The server sent a frame that is too long");
              report_error (connection, error);
              g_error_free (error);
              return;
            }

          if (payload_start + payload_length
              > connection->input_buffer + connection->input_length)
            break;

          /* Ignore control frames and non-binary frames */
          if (*p == 0x82
              && !process_message (connection,
                                   payload_start, payload_length,
                                   &error))
            {
              report_error (connection, error);
              g_error_free (error);
              return;
            }

          p = payload_start + payload_length;
        }

      memmove (connection->input_buffer,
               p,
               connection->input_buffer + connection->input_length - p);
      connection->input_length -= p - connection->input_buffer;

      update_poll (connection);
    }
}

static int
write_ws_request (VsxConnection *connection,
                  uint8_t *buffer,
                  size_t buffer_size)
{
  static const uint8_t ws_request[] =
    "GET / HTTP/1.1\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
    "\r\n";
  const size_t ws_request_len = (sizeof ws_request) - 1;

  if (buffer_size < ws_request_len)
    return -1;

  memcpy (buffer, ws_request, ws_request_len);

  return ws_request_len;
}

static int
write_header (VsxConnection *connection,
              uint8_t *buffer,
              size_t buffer_size)
{
  if (connection->has_person_id)
    {
      return vsx_proto_write_command (buffer,
                                      buffer_size,

                                      VSX_PROTO_RECONNECT,

                                      VSX_PROTO_TYPE_UINT64,
                                      connection->person_id,

                                      VSX_PROTO_TYPE_UINT16,
                                      connection->next_message_num,

                                      VSX_PROTO_TYPE_NONE);
    }
  else
    {
      return vsx_proto_write_command (buffer,
                                      buffer_size,

                                      VSX_PROTO_NEW_PLAYER,

                                      VSX_PROTO_TYPE_STRING,
                                      connection->room,

                                      VSX_PROTO_TYPE_STRING,
                                      connection->player_name,

                                      VSX_PROTO_TYPE_NONE);
    }
}

static int
write_keep_alive (VsxConnection *connection,
                  uint8_t *buffer,
                  size_t buffer_size)
{
  return vsx_proto_write_command (buffer,
                                  buffer_size,

                                  VSX_PROTO_KEEP_ALIVE,

                                  VSX_PROTO_TYPE_NONE);
}

static int
write_leave (VsxConnection *connection,
             uint8_t *buffer,
             size_t buffer_size)
{
  return vsx_proto_write_command (buffer,
                                  buffer_size,

                                  VSX_PROTO_LEAVE,

                                  VSX_PROTO_TYPE_NONE);
}

static int
write_shout (VsxConnection *connection,
             uint8_t *buffer,
             size_t buffer_size)
{
  return vsx_proto_write_command (buffer,
                                  buffer_size,

                                  VSX_PROTO_SHOUT,

                                  VSX_PROTO_TYPE_NONE);
}

static int
write_turn (VsxConnection *connection,
            uint8_t *buffer,
            size_t buffer_size)
{
  return vsx_proto_write_command (buffer,
                                  buffer_size,

                                  VSX_PROTO_TURN,

                                  VSX_PROTO_TYPE_NONE);
}

static int
write_move_tile (VsxConnection *connection,
                 uint8_t *buffer,
                 size_t buffer_size)
{
  if (vsx_list_empty (&connection->tiles_to_move))
    return 0;

  VsxConnectionTileToMove *tile =
    vsx_container_of (connection->tiles_to_move.next,
                      tile,
                      link);

  int ret = vsx_proto_write_command (buffer,
                                     buffer_size,

                                     VSX_PROTO_MOVE_TILE,

                                     VSX_PROTO_TYPE_UINT8,
                                     tile->num,

                                     VSX_PROTO_TYPE_INT16,
                                     tile->x,

                                     VSX_PROTO_TYPE_INT16,
                                     tile->y,

                                     VSX_PROTO_TYPE_NONE);

  if (ret > 0)
    {
      vsx_list_remove (&tile->link);
      g_free (tile);
    }

  return ret;
}

static int
write_send_message (VsxConnection *connection,
                    uint8_t *buffer,
                    size_t buffer_size)
{
  if (vsx_list_empty (&connection->messages_to_send))
    return 0;

  VsxConnectionMessageToSend *message =
    vsx_container_of (connection->messages_to_send.next, message, link);

  int ret = vsx_proto_write_command (buffer,
                                     buffer_size,

                                     VSX_PROTO_SEND_MESSAGE,

                                     VSX_PROTO_TYPE_STRING,
                                     message->message,

                                     VSX_PROTO_TYPE_NONE);

  if (ret > 0)
    {
      /* The server automatically assumes we're not typing anymore
         when the client sends a message */
      connection->sent_typing_state = false;

      vsx_list_remove (&message->link);
      g_free (message);
    }

  return ret;
}

static int
write_typing_state (VsxConnection *connection,
                    uint8_t *buffer,
                    size_t buffer_size)
{
  if (connection->typing == connection->sent_typing_state)
    return 0;

  int ret = vsx_proto_write_command (buffer,
                                     buffer_size,

                                     connection->typing
                                     ? VSX_PROTO_START_TYPING
                                     : VSX_PROTO_STOP_TYPING,

                                     VSX_PROTO_TYPE_NONE);

  if (ret > 0)
    connection->sent_typing_state = connection->typing;

  return ret;
}

static void
fill_output_buffer (VsxConnection *connection)
{
  static const struct
  {
    VsxConnectionDirtyFlag flag;
    VsxConnectionWriteStateFunc func;
  } write_funcs[] =
    {
      { VSX_CONNECTION_DIRTY_FLAG_WS_HEADER, write_ws_request },
      { VSX_CONNECTION_DIRTY_FLAG_HEADER, write_header },
      { VSX_CONNECTION_DIRTY_FLAG_KEEP_ALIVE, write_keep_alive },
      { VSX_CONNECTION_DIRTY_FLAG_LEAVE, write_leave },
      { VSX_CONNECTION_DIRTY_FLAG_SHOUT, write_shout },
      { VSX_CONNECTION_DIRTY_FLAG_TURN, write_turn },
      { .func = write_move_tile },
      { .func = write_send_message },
      { .func = write_typing_state },
    };

  while (true)
    {
      for (int i = 0; i < VSX_N_ELEMENTS (write_funcs); i++)
        {
          if (write_funcs[i].flag != 0 &&
              (connection->dirty_flags & write_funcs[i].flag) == 0)
            continue;

          int wrote = write_funcs[i].func (connection,
                                           connection->output_buffer
                                           + connection->output_length,
                                           (sizeof connection->output_buffer)
                                           - connection->output_length);

          if (wrote == -1)
            return;

          connection->dirty_flags &= ~write_funcs[i].flag;

          if (wrote == 0)
            continue;

          connection->output_length += wrote;

          goto found;
        }

      return;

    found:
      continue;
    }
}

static void
handle_write (VsxConnection *connection)
{
  fill_output_buffer (connection);

  size_t wrote = write (connection->sock,
                        connection->output_buffer,
                        connection->output_length);

  if (wrote == -1)
    {
      if (!is_would_block_error (errno) && errno != EINTR)
        {
          GError *error = NULL;

          g_set_error (&error,
                       G_FILE_ERROR,
                       g_file_error_from_errno (errno),
                       "Error writing to socket: %s",
                       strerror (errno));

          report_error (connection, error);

          g_clear_error (&error);
        }
    }
  else
    {
      /* Move any remaining data in the output buffer to the front */
      memmove (connection->output_buffer,
               connection->output_buffer + wrote,
               connection->output_length - wrote);
      connection->output_length -= wrote;

      vsx_connection_queue_keep_alive (connection);

      update_poll (connection);
    }
}

static gboolean
sock_source_cb (GIOChannel *source,
                GIOCondition condition,
                gpointer data)
{
  VsxConnection *connection = data;

  if (connection->running_state == VSX_CONNECTION_RUNNING_STATE_RECONNECTING
      && (condition & G_IO_OUT))
    connection->running_state = VSX_CONNECTION_RUNNING_STATE_RUNNING;

  if ((condition & (G_IO_IN |
                    G_IO_ERR |
                    G_IO_HUP)))
    handle_read (connection);
  else if ((condition & G_IO_OUT))
    handle_write (connection);
  else
    update_poll (connection);

  return true;
}

static void
set_sock_condition (VsxConnection *connection,
                    GIOCondition condition)
{
  if (condition == connection->sock_condition)
    return;

  if (connection->sock_source)
    g_source_remove (connection->sock_source);

  connection->sock_source = g_io_add_watch (connection->sock_channel,
                                            condition,
                                            sock_source_cb,
                                            connection);
  connection->sock_condition = condition;
}

static bool
has_pending_data (VsxConnection *connection)
{
  if (connection->output_length > 0)
    return true;

  if (connection->dirty_flags)
    return true;

  if (!vsx_list_empty (&connection->tiles_to_move))
    return true;

  if (!vsx_list_empty (&connection->messages_to_send))
    return true;

  if (connection->sent_typing_state != connection->typing)
    return true;

  return false;
}

static void
update_poll (VsxConnection *connection)
{
  GIOCondition condition;

  switch (connection->running_state)
    {
    case VSX_CONNECTION_RUNNING_STATE_RECONNECTING:
      condition = G_IO_OUT;
      break;

    case VSX_CONNECTION_RUNNING_STATE_RUNNING:
      condition = G_IO_IN;

      if (!connection->write_finished)
        {
          if (has_pending_data (connection))
            {
              condition = G_IO_OUT;
            }
          else if (connection->self
                   && !vsx_player_is_connected (connection->self))
            {
              shutdown (connection->sock, SHUT_WR);

              connection->write_finished = true;
            }
        }
      break;

    default:
      return;
    }

  set_sock_condition (connection, condition);
}

static gboolean
vsx_connection_reconnect_cb (gpointer user_data)
{
  VsxConnection *connection = user_data;

  connection->reconnect_handler = 0;

  close_socket (connection);

  GError *error = NULL;

  struct vsx_netaddress_native address;

  if (!g_socket_address_to_native (connection->address,
                                   &address.sockaddr,
                                   offsetof (struct vsx_netaddress_native,
                                             length),
                                   &error))
    {
      report_error (connection, error);
      g_error_free (error);
      return FALSE;
    }

  address.length = g_socket_address_get_native_size (connection->address);

  connection->sock = socket (address.sockaddr.sa_family == AF_INET6
                             ? PF_INET6
                             : PF_INET,
                             SOCK_STREAM,
                             0);

  if (connection->sock == -1)
      goto error;

  struct vsx_error *socket_error = NULL;

  if (!vsx_socket_set_nonblock (connection->sock, &socket_error))
    {
      /* FIXME */
      errno = EINVAL;
      vsx_error_free (socket_error);
      goto error;
    }

  int connect_ret = connect (connection->sock,
                             &address.sockaddr,
                             address.length);

  GIOCondition condition;

  if (connect_ret == 0)
    {
      connection->running_state = VSX_CONNECTION_RUNNING_STATE_RUNNING;
      condition = G_IO_IN | G_IO_OUT;
    }
  else if (errno == EINPROGRESS)
    {
      connection->running_state = VSX_CONNECTION_RUNNING_STATE_RECONNECTING;
      condition = G_IO_OUT;
    }
  else
    {
      goto error;
    }

  connection->dirty_flags |= (VSX_CONNECTION_DIRTY_FLAG_WS_HEADER
                              | VSX_CONNECTION_DIRTY_FLAG_HEADER);
  connection->ws_terminator_pos = 0;
  connection->write_finished = false;

  connection->sock_channel = g_io_channel_unix_new (connection->sock);

  g_io_channel_set_encoding (connection->sock_channel,
                             NULL /* encoding */,
                             NULL /* error */);
  g_io_channel_set_buffered (connection->sock_channel, false);

  set_sock_condition (connection, condition);

  /* Remove the handler */
  return false;

 error:
  g_set_error (&error,
               G_FILE_ERROR,
               g_file_error_from_errno (errno),
               "Error connecting: %s",
               strerror (errno));

  report_error (connection, error);

  g_clear_error (&error);

  return false;
}

static void
vsx_connection_queue_reconnect (VsxConnection *connection)
{
  assert (connection->reconnect_handler == 0);

  connection->reconnect_handler =
    g_timeout_add_seconds (connection->reconnect_timeout,
                           vsx_connection_reconnect_cb,
                           connection);
  /* Next time we need to try to reconnect we'll delay for twice
     as long, up to the maximum timeout */
  connection->reconnect_timeout *= 2;
  if (connection->reconnect_timeout > VSX_CONNECTION_MAX_TIMEOUT)
    connection->reconnect_timeout = VSX_CONNECTION_MAX_TIMEOUT;

  connection->running_state =
    VSX_CONNECTION_RUNNING_STATE_WAITING_FOR_RECONNECT;
}

static void
vsx_connection_set_running_internal (VsxConnection *connection,
                                     bool running)
{
  if (running)
    {
      if (connection->running_state
          == VSX_CONNECTION_RUNNING_STATE_DISCONNECTED)
        {
          /* Reset the retry timeout because this is a first attempt
             at connecting */
          connection->reconnect_timeout = VSX_CONNECTION_INITIAL_TIMEOUT;

          connection->running_state =
            VSX_CONNECTION_RUNNING_STATE_WAITING_FOR_RECONNECT;

          assert (connection->reconnect_handler == 0);
          connection->reconnect_handler =
            g_idle_add (vsx_connection_reconnect_cb,
                        connection);
        }
    }
  else
    {
      switch (connection->running_state)
        {
        case VSX_CONNECTION_RUNNING_STATE_DISCONNECTED:
          /* already disconnected */
          break;

        case VSX_CONNECTION_RUNNING_STATE_RECONNECTING:
        case VSX_CONNECTION_RUNNING_STATE_RUNNING:
          connection->running_state =
            VSX_CONNECTION_RUNNING_STATE_DISCONNECTED;
          close_socket (connection);
          break;

        case VSX_CONNECTION_RUNNING_STATE_WAITING_FOR_RECONNECT:
          /* Cancel the timeout */
          g_source_remove (connection->reconnect_handler);
          connection->running_state = VSX_CONNECTION_RUNNING_STATE_DISCONNECTED;
          break;
        }
    }
}

void
vsx_connection_set_running (VsxConnection *connection,
                            bool running)
{
  vsx_connection_set_running_internal (connection, running);

  VsxConnectionEvent event =
    {
      .type = VSX_CONNECTION_EVENT_TYPE_RUNNING_STATE_CHANGED,
      .running_state_changed = { .running = running },
    };

  vsx_signal_emit (&connection->event_signal, &event);
}

bool
vsx_connection_get_running (VsxConnection *connection)
{
  return connection->running_state != VSX_CONNECTION_RUNNING_STATE_DISCONNECTED;
}

VsxConnection *
vsx_connection_new (GSocketAddress *address,
                    const char *room,
                    const char *player_name)
{
  VsxConnection *connection = vsx_calloc (sizeof *connection);

  if (address == NULL)
    {
      GInetAddress *localhost =
        g_inet_address_new_loopback (G_SOCKET_FAMILY_IPV4);
      connection->address = g_inet_socket_address_new (localhost, 5144);

      g_object_unref (localhost);
    }
  else
    {
      connection->address = g_object_ref (address);
    }

  vsx_signal_init (&connection->event_signal);

  connection->sock = -1;

  connection->room = vsx_strdup (room);
  connection->player_name = vsx_strdup (player_name);

  connection->next_message_num = 0;

  connection->keep_alive_time = g_timer_new ();

  vsx_buffer_init (&connection->players);

  vsx_slab_init (&connection->tile_allocator);
  vsx_buffer_init (&connection->tiles);

  vsx_list_init (&connection->tiles_to_move);
  vsx_list_init (&connection->messages_to_send);

  return connection;
}

static void
free_tiles_to_move (VsxConnection *connection)
{
  VsxConnectionTileToMove *tile, *tmp;

  vsx_list_for_each_safe (tile, tmp, &connection->tiles_to_move, link)
    {
      g_free (tile);
    }
}

static void
free_messages_to_send (VsxConnection *connection)
{
  VsxConnectionMessageToSend *message, *tmp;

  vsx_list_for_each_safe (message, tmp, &connection->messages_to_send, link)
    {
      g_free (message);
    }
}

static void
free_players (VsxConnection *connection)
{
  for (int i = 0; i < connection->players.length / sizeof (VsxPlayer *); i++)
    {
      VsxPlayer *player = ((VsxPlayer **) connection->players.data)[i];

      if (player == NULL)
        continue;

      g_free (player->name);
      vsx_free (player);
    }

  vsx_buffer_destroy (&connection->players);
}

void
vsx_connection_free (VsxConnection *connection)
{
  vsx_connection_set_running_internal (connection, false);

  if (connection->address)
    {
      g_object_unref (connection->address);
      connection->address = NULL;
    }

  vsx_free (connection->room);
  vsx_free (connection->player_name);

  g_timer_destroy (connection->keep_alive_time);

  free_players (connection);

  vsx_buffer_destroy (&connection->tiles);
  vsx_slab_destroy (&connection->tile_allocator);

  free_tiles_to_move (connection);
  free_messages_to_send (connection);

  vsx_free (connection);
}

bool
vsx_connection_get_typing (VsxConnection *connection)
{
  return connection->typing;
}

VsxConnectionState
vsx_connection_get_state (VsxConnection *connection)
{
  return connection->state;
}

void
vsx_connection_send_message (VsxConnection *connection,
                             const char *message)
{
  size_t message_length = strlen (message);

  if (message_length > VSX_PROTO_MAX_MESSAGE_LENGTH)
    {
      message_length = VSX_PROTO_MAX_MESSAGE_LENGTH;
      /* If we’ve clipped before a continuation byte then also clip
       * the rest of the UTF-8 sequence so that it will remain valid
       * UTF-8. */
      while ((message[message_length] & 0xc0) == 0x80)
        message_length--;
    }

  VsxConnectionMessageToSend *message_to_send =
    g_malloc (offsetof (VsxConnectionMessageToSend, message)
              + message_length + 1);

  memcpy (message_to_send->message, message, message_length);
  message_to_send->message[message_length] = '\0';

  vsx_list_insert (connection->messages_to_send.prev, &message_to_send->link);

  update_poll (connection);
}

void
vsx_connection_leave (VsxConnection *connection)
{
  connection->dirty_flags |= VSX_CONNECTION_DIRTY_FLAG_LEAVE;

  update_poll (connection);
}

const VsxPlayer *
vsx_connection_get_player (VsxConnection *connection,
                           int player_num)
{
  return get_pointer_from_buffer (&connection->players, player_num);
}

void
vsx_connection_foreach_player (VsxConnection *connection,
                               VsxConnectionForeachPlayerCallback callback,
                               void *user_data)
{
  for (int i = 0; i < connection->players.length / sizeof (VsxPlayer *); i++)
    {
      VsxPlayer *player = ((VsxPlayer **) connection->players.data)[i];

      if (player == NULL)
        continue;

      callback (player, user_data);
    }
}

const VsxPlayer *
vsx_connection_get_self (VsxConnection *connection)
{
  return connection->self;
}

const VsxTile *
vsx_connection_get_tile (VsxConnection *connection,
                         int tile_num)
{
  return get_pointer_from_buffer (&connection->tiles, tile_num);
}

void
vsx_connection_foreach_tile (VsxConnection *connection,
                             VsxConnectionForeachTileCallback callback,
                             void *user_data)
{
  for (int i = 0; i < connection->tiles.length / sizeof (VsxTile *); i++)
    {
      VsxTile *tile = ((VsxTile **) connection->tiles.data)[i];

      if (tile == NULL)
        continue;

      callback (tile, user_data);
    }
}

VsxSignal *
vsx_connection_get_event_signal (VsxConnection *connection)
{
  return &connection->event_signal;
}

GQuark
vsx_connection_error_quark (void)
{
  return g_quark_from_static_string ("vsx-connection-error-quark");
}
