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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "vsx-connection.h"

#include <string.h>
#include <stdarg.h>
#include <gio/gio.h>
#include <assert.h>

#include "vsx-marshal.h"
#include "vsx-enum-types.h"
#include "vsx-player-private.h"
#include "vsx-tile-private.h"
#include "vsx-proto.h"
#include "vsx-list.h"

enum
{
  PROP_0,

  PROP_ADDRESS,
  PROP_ROOM,
  PROP_PLAYER_NAME,
  PROP_RUNNING,
  PROP_TYPING,
  PROP_STATE
};

enum
{
  SIGNAL_GOT_ERROR,
  SIGNAL_MESSAGE,
  SIGNAL_PLAYER_CHANGED,
  SIGNAL_PLAYER_SHOUTED,
  SIGNAL_TILE_CHANGED,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

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
vsx_connection_dispose (GObject *object);

static void
vsx_connection_finalize (GObject *object);

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
  /* g_socket_connect has been called and we are waiting for it to
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

struct _VsxConnectionPrivate
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

  VsxConnectionDirtyFlag dirty_flags;
  VsxList tiles_to_move;
  VsxList messages_to_send;

  GSocket *sock;
  GIOChannel *sock_channel;
  guint sock_source;
  /* The condition that the source was last created with so we can
   * know if we need to recreate it.
   */
  GIOCondition sock_condition;

  GHashTable *players;
  GHashTable *tiles;

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

G_DEFINE_TYPE_WITH_CODE (VsxConnection,
                         vsx_connection,
                         G_TYPE_OBJECT,
                         G_ADD_PRIVATE (VsxConnection))

static gboolean
vsx_connection_keep_alive_cb (void *data)
{
  VsxConnection *connection = data;
  VsxConnectionPrivate *priv = connection->priv;

  priv->keep_alive_timeout = 0;

  priv->dirty_flags |= VSX_CONNECTION_DIRTY_FLAG_KEEP_ALIVE;

  update_poll (connection);

  return false;
}

static void
vsx_connection_queue_keep_alive (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  if (priv->keep_alive_timeout)
    g_source_remove (priv->keep_alive_timeout);

  priv->keep_alive_timeout
    = g_timeout_add_seconds (VSX_CONNECTION_KEEP_ALIVE_TIME + 1,
                             vsx_connection_keep_alive_cb,
                             connection);

  g_timer_start (priv->keep_alive_time);
}

static void
vsx_connection_signal_error (VsxConnection *connection,
                             GError *error)
{
  g_signal_emit (connection,
                 signals[SIGNAL_GOT_ERROR],
                 0, /* detail */
                 error);
}

static void
close_socket (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  if (priv->keep_alive_timeout)
    {
      g_source_remove (priv->keep_alive_timeout);
      priv->keep_alive_timeout = 0;
    }

  if (priv->sock_source)
    {
      g_source_remove (priv->sock_source);
      priv->sock_source = 0;
      priv->sock_condition = 0;
    }

  if (priv->sock_channel)
    {
      g_io_channel_unref (priv->sock_channel);
      priv->sock_channel = NULL;
    }

  if (priv->sock)
    {
      g_socket_close (priv->sock, NULL);
      g_object_unref (priv->sock);
      priv->sock = NULL;
    }
}

static void
vsx_connection_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
  VsxConnection *connection = VSX_CONNECTION (object);

  switch (prop_id)
    {
    case PROP_RUNNING:
      g_value_set_boolean (value, vsx_connection_get_running (connection));
      break;

    case PROP_TYPING:
      g_value_set_boolean (value,
                           vsx_connection_get_typing (connection));
      break;

    case PROP_STATE:
      g_value_set_enum (value,
                        vsx_connection_get_state (connection));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
vsx_connection_set_property (GObject *object,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
  VsxConnection *connection = VSX_CONNECTION (object);
  VsxConnectionPrivate *priv = connection->priv;

  switch (prop_id)
    {
    case PROP_ROOM:
      priv->room = g_strdup (g_value_get_string (value));
      break;

    case PROP_ADDRESS:
      priv->address = g_value_get_object (value);
      if (priv->address)
        g_object_ref (priv->address);
      break;

    case PROP_PLAYER_NAME:
      priv->player_name = g_strdup (g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

void
vsx_connection_set_typing (VsxConnection *connection,
                           bool typing)
{
  VsxConnectionPrivate *priv = connection->priv;

  if (priv->typing != typing)
    {
      priv->typing = typing;
      update_poll (connection);
      g_object_notify (G_OBJECT (connection), "typing");
    }
}

void
vsx_connection_shout (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  priv->dirty_flags |= VSX_CONNECTION_DIRTY_FLAG_SHOUT;

  update_poll (connection);
}

void
vsx_connection_turn (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  priv->dirty_flags |= VSX_CONNECTION_DIRTY_FLAG_TURN;

  update_poll (connection);
}

void
vsx_connection_move_tile (VsxConnection *connection,
                          int tile_num,
                          int x,
                          int y)
{
  VsxConnectionPrivate *priv = connection->priv;

  VsxConnectionTileToMove *tile;

  vsx_list_for_each (tile, &priv->tiles_to_move, link)
    {
      if (tile->num == tile_num)
        goto found_tile;
    }

  tile = g_new (VsxConnectionTileToMove, 1);
  tile->num = tile_num;
  vsx_list_insert (priv->tiles_to_move.prev, &tile->link);

 found_tile:
  tile->x = x;
  tile->y = y;

  update_poll (connection);
}

static void
vsx_connection_set_state (VsxConnection *connection,
                          VsxConnectionState state)
{
  VsxConnectionPrivate *priv = connection->priv;

  if (priv->state != state)
    {
      priv->state = state;
      g_object_notify (G_OBJECT (connection), "state");
    }
}

static VsxPlayer *
get_or_create_player (VsxConnection *connection,
                      int player_num)
{
  VsxConnectionPrivate *priv = connection->priv;
  VsxPlayer *player =
    g_hash_table_lookup (priv->players, GINT_TO_POINTER (player_num));

  if (player == NULL)
    {
      player = g_slice_new0 (VsxPlayer);

      player->num = player_num;

      g_hash_table_insert (priv->players,
                           GINT_TO_POINTER (player_num),
                           player);
    }

  return player;
}

static bool
handle_player_id (VsxConnection *connection,
                  const uint8_t *payload,
                  size_t payload_length,
                  GError **error)
{
  VsxConnectionPrivate *priv = connection->priv;
  uint8_t self_num;

  if (!vsx_proto_read_payload (payload + 1,
                               payload_length - 1,

                               VSX_PROTO_TYPE_UINT64,
                               &priv->person_id,

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

  priv->self = get_or_create_player (connection, self_num);

  priv->has_person_id = true;

  if (priv->state == VSX_CONNECTION_STATE_AWAITING_HEADER)
    vsx_connection_set_state (connection, VSX_CONNECTION_STATE_IN_PROGRESS);

  return true;
}

static bool
handle_message (VsxConnection *connection,
                const uint8_t *payload,
                size_t payload_length,
                GError **error)
{
  VsxConnectionPrivate *priv = connection->priv;
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

  priv->next_message_num++;

  g_signal_emit (connection,
                 signals[SIGNAL_MESSAGE],
                 0, /* detail */
                 get_or_create_player (connection, person),
                 text);

  return true;
}

static bool
handle_tile (VsxConnection *connection,
             const uint8_t *payload,
             size_t payload_length,
             GError **error)
{
  VsxConnectionPrivate *priv = connection->priv;
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
      || g_utf8_strlen (letter, -1) != 1)
    {
      g_set_error (error,
                   VSX_CONNECTION_ERROR,
                   VSX_CONNECTION_ERROR_BAD_DATA,
                   "The server sent an invalid tile command");
      return false;
    }

  VsxTile *tile = g_hash_table_lookup (priv->tiles,
                                       GINT_TO_POINTER ((int) num));
  bool is_new = false;

  if (tile == NULL)
    {
      tile = g_slice_new0 (VsxTile);
      tile->num = num;

      g_hash_table_insert (priv->tiles, GINT_TO_POINTER ((int) num), tile);
      is_new = true;
    }

  tile->x = x;
  tile->y = y;
  tile->letter = g_utf8_get_char (letter);

  g_signal_emit (connection,
                 signals[SIGNAL_TILE_CHANGED],
                 0, /* detail */
                 is_new,
                 tile);

  return true;
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

  g_signal_emit (connection,
                 signals[SIGNAL_PLAYER_CHANGED],
                 0, /* detail */
                 player);

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

  g_signal_emit (connection,
                 signals[SIGNAL_PLAYER_CHANGED],
                 0, /* detail */
                 player);

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

  VsxPlayer *player = get_or_create_player (connection, player_num);

  g_signal_emit (connection,
                 signals[SIGNAL_PLAYER_SHOUTED],
                 0, /* detail */
                 player);

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

static void
handle_read (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;
  GError *error = NULL;

  gssize got = g_socket_receive (priv->sock,
                                 (char *) priv->input_buffer
                                 + priv->input_length,
                                 (sizeof priv->input_buffer)
                                 - priv->input_length,
                                 NULL, /* cancellable */
                                 &error);
  if (got == -1)
    {
      if (error->domain != G_IO_ERROR
          || error->code != G_IO_ERROR_WOULD_BLOCK)
          report_error (connection, error);

      g_clear_error (&error);
    }
  else if (got == 0)
    {
      if (priv->state == VSX_CONNECTION_STATE_DONE)
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
      const uint8_t *p = priv->input_buffer;

      priv->input_length += got;

      if (priv->ws_terminator_pos < WS_TERMINATOR_LENGTH)
        {
          while (p - priv->input_buffer < priv->input_length)
            {
              if (*(p++) == ws_terminator[priv->ws_terminator_pos])
                {
                  if (++priv->ws_terminator_pos >= WS_TERMINATOR_LENGTH)
                    goto terminated;
                }
              else
                {
                  priv->ws_terminator_pos = 0;
                }
            }

          /* If we make it here then we haven’t found the end of the
           * terminator yet.
           */
          priv->input_length = 0;
          return;
        }

    terminated: ((void) 0);

      size_t payload_length;
      const uint8_t *payload_start;

      while (get_payload_length (p,
                                 priv->input_buffer
                                 + priv->input_length
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
              > priv->input_buffer + priv->input_length)
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

      memmove (priv->input_buffer,
               p,
               priv->input_buffer + priv->input_length - p);
      priv->input_length -= p - priv->input_buffer;

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
  VsxConnectionPrivate *priv = connection->priv;

  if (priv->has_person_id)
    {
      return vsx_proto_write_command (buffer,
                                      buffer_size,

                                      VSX_PROTO_RECONNECT,

                                      VSX_PROTO_TYPE_UINT64,
                                      priv->person_id,

                                      VSX_PROTO_TYPE_UINT16,
                                      priv->next_message_num,

                                      VSX_PROTO_TYPE_NONE);
    }
  else
    {
      return vsx_proto_write_command (buffer,
                                      buffer_size,

                                      VSX_PROTO_NEW_PLAYER,

                                      VSX_PROTO_TYPE_STRING,
                                      priv->room,

                                      VSX_PROTO_TYPE_STRING,
                                      priv->player_name,

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
  VsxConnectionPrivate *priv = connection->priv;

  if (vsx_list_empty (&priv->tiles_to_move))
    return 0;

  VsxConnectionTileToMove *tile = vsx_container_of (priv->tiles_to_move.next,
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
  VsxConnectionPrivate *priv = connection->priv;

  if (vsx_list_empty (&priv->messages_to_send))
    return 0;

  VsxConnectionMessageToSend *message =
    vsx_container_of (priv->messages_to_send.next, message, link);

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
      priv->sent_typing_state = false;

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
  VsxConnectionPrivate *priv = connection->priv;

  if (priv->typing == priv->sent_typing_state)
    return 0;

  int ret = vsx_proto_write_command (buffer,
                                     buffer_size,

                                     priv->typing
                                     ? VSX_PROTO_START_TYPING
                                     : VSX_PROTO_STOP_TYPING,

                                     VSX_PROTO_TYPE_NONE);

  if (ret > 0)
    priv->sent_typing_state = priv->typing;

  return ret;
}

static void
fill_output_buffer (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

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
      for (int i = 0; i < G_N_ELEMENTS (write_funcs); i++)
        {
          if (write_funcs[i].flag != 0 &&
              (priv->dirty_flags & write_funcs[i].flag) == 0)
            continue;

          int wrote = write_funcs[i].func (connection,
                                           priv->output_buffer
                                           + priv->output_length,
                                           (sizeof priv->output_buffer)
                                           - priv->output_length);

          if (wrote == -1)
            return;

          priv->dirty_flags &= ~write_funcs[i].flag;

          if (wrote == 0)
            continue;

          priv->output_length += wrote;

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
  VsxConnectionPrivate *priv = connection->priv;

  fill_output_buffer (connection);

  GError *error = NULL;

  gssize wrote = g_socket_send (priv->sock,
                                (const gchar *) priv->output_buffer,
                                priv->output_length,
                                NULL, /* cancellable */
                                &error);

  if (wrote == -1)
    {
      if (error->domain != G_IO_ERROR
          || error->code != G_IO_ERROR_WOULD_BLOCK)
        {
          report_error (connection, error);
          g_error_free (error);
        }
    }
  else
    {
      /* Move any remaining data in the output buffer to the front */
      memmove (priv->output_buffer,
               priv->output_buffer + wrote,
               priv->output_length - wrote);
      priv->output_length -= wrote;

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
  VsxConnectionPrivate *priv = connection->priv;

  if (priv->running_state == VSX_CONNECTION_RUNNING_STATE_RECONNECTING)
    {
      GError *error = NULL;

      if (!g_socket_check_connect_result (priv->sock, &error))
        {
          report_error (connection, error);
          g_error_free (error);
          return true;
        }

      priv->running_state = VSX_CONNECTION_RUNNING_STATE_RUNNING;
    }

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
  VsxConnectionPrivate *priv = connection->priv;

  if (condition == priv->sock_condition)
    return;

  if (priv->sock_source)
    g_source_remove (priv->sock_source);

  priv->sock_source = g_io_add_watch (priv->sock_channel,
                                      condition,
                                      sock_source_cb,
                                      connection);
  priv->sock_condition = condition;
}

static bool
has_pending_data (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  if (priv->output_length > 0)
    return true;

  if (priv->dirty_flags)
    return true;

  if (!vsx_list_empty (&priv->tiles_to_move))
    return true;

  if (!vsx_list_empty (&priv->messages_to_send))
    return true;

  if (priv->sent_typing_state != priv->typing)
    return true;

  return false;
}

static void
update_poll (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  GIOCondition condition;

  switch (priv->running_state)
    {
    case VSX_CONNECTION_RUNNING_STATE_RECONNECTING:
      condition = G_IO_OUT;
      break;

    case VSX_CONNECTION_RUNNING_STATE_RUNNING:
      condition = G_IO_IN;

      if (!priv->write_finished)
        {
          if (has_pending_data (connection))
            {
              condition = G_IO_OUT;
            }
          else if (priv->self
                   && !vsx_player_is_connected (priv->self))
            {
              g_socket_shutdown (priv->sock,
                                 false, /* shutdown read */
                                 true, /* shutdown write */
                                 NULL /* error */);

              priv->write_finished = true;
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
  VsxConnectionPrivate *priv = connection->priv;

  priv->reconnect_handler = 0;

  close_socket (connection);

  GError *error = NULL;

  priv->sock = g_socket_new (G_SOCKET_FAMILY_IPV4,
                             G_SOCKET_TYPE_STREAM,
                             G_SOCKET_PROTOCOL_DEFAULT,
                             &error);

  if (priv->sock == NULL)
    {
      report_error (connection, error);
      g_error_free (error);
      return false;
    }

  g_socket_set_blocking (priv->sock, false);

  bool connect_ret = g_socket_connect (priv->sock,
                                       priv->address,
                                       NULL, /* cancellable */
                                       &error);

  GIOCondition condition;

  if (connect_ret)
    {
      priv->running_state = VSX_CONNECTION_RUNNING_STATE_RUNNING;
      condition = G_IO_IN | G_IO_OUT;
    }
  else if (error->domain == G_IO_ERROR
           && error->code == G_IO_ERROR_PENDING)
    {
      g_error_free (error);
      priv->running_state = VSX_CONNECTION_RUNNING_STATE_RECONNECTING;
      condition = G_IO_OUT;
    }
  else
    {
      report_error (connection, error);
      g_error_free (error);
      return false;
    }

  priv->dirty_flags |= (VSX_CONNECTION_DIRTY_FLAG_WS_HEADER
                        | VSX_CONNECTION_DIRTY_FLAG_HEADER);
  priv->ws_terminator_pos = 0;
  priv->write_finished = false;

  priv->sock_channel = g_io_channel_unix_new (g_socket_get_fd (priv->sock));

  g_io_channel_set_encoding (priv->sock_channel,
                             NULL /* encoding */,
                             NULL /* error */);
  g_io_channel_set_buffered (priv->sock_channel, false);

  set_sock_condition (connection, condition);

  /* Remove the handler */
  return false;
}

static void
vsx_connection_queue_reconnect (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  assert (priv->reconnect_handler == 0);

  priv->reconnect_handler =
    g_timeout_add_seconds (priv->reconnect_timeout,
                           vsx_connection_reconnect_cb,
                           connection);
  /* Next time we need to try to reconnect we'll delay for twice
     as long, up to the maximum timeout */
  priv->reconnect_timeout *= 2;
  if (priv->reconnect_timeout > VSX_CONNECTION_MAX_TIMEOUT)
    priv->reconnect_timeout = VSX_CONNECTION_MAX_TIMEOUT;

  priv->running_state = VSX_CONNECTION_RUNNING_STATE_WAITING_FOR_RECONNECT;
}

static void
vsx_connection_constructed (GObject *object)
{
  VsxConnection *connection = VSX_CONNECTION (object);
  VsxConnectionPrivate *priv = connection->priv;

  if (priv->address == NULL)
    {
      GInetAddress *localhost =
        g_inet_address_new_loopback (G_SOCKET_FAMILY_IPV4);
      priv->address = g_inet_socket_address_new (localhost, 5144);

      g_object_unref (localhost);
    }
}

static void
vsx_connection_set_running_internal (VsxConnection *connection,
                                     bool running)
{
  VsxConnectionPrivate *priv = connection->priv;

  if (running)
    {
      if (priv->running_state == VSX_CONNECTION_RUNNING_STATE_DISCONNECTED)
        {
          /* Reset the retry timeout because this is a first attempt
             at connecting */
          priv->reconnect_timeout = VSX_CONNECTION_INITIAL_TIMEOUT;

          priv->running_state =
            VSX_CONNECTION_RUNNING_STATE_WAITING_FOR_RECONNECT;

          assert (priv->reconnect_handler == 0);
          priv->reconnect_handler = g_idle_add (vsx_connection_reconnect_cb,
                                                connection);
        }
    }
  else
    {
      switch (priv->running_state)
        {
        case VSX_CONNECTION_RUNNING_STATE_DISCONNECTED:
          /* already disconnected */
          break;

        case VSX_CONNECTION_RUNNING_STATE_RECONNECTING:
        case VSX_CONNECTION_RUNNING_STATE_RUNNING:
          priv->running_state =
            VSX_CONNECTION_RUNNING_STATE_DISCONNECTED;
          close_socket (connection);
          break;

        case VSX_CONNECTION_RUNNING_STATE_WAITING_FOR_RECONNECT:
          /* Cancel the timeout */
          g_source_remove (priv->reconnect_handler);
          priv->running_state = VSX_CONNECTION_RUNNING_STATE_DISCONNECTED;
          break;
        }
    }
}

void
vsx_connection_set_running (VsxConnection *connection,
                            bool running)
{
  g_return_if_fail (VSX_IS_CONNECTION (connection));

  vsx_connection_set_running_internal (connection, running);

  g_object_notify (G_OBJECT (connection), "running");
}

bool
vsx_connection_get_running (VsxConnection *connection)
{
  g_return_val_if_fail (VSX_IS_CONNECTION (connection), false);

  return (connection->priv->running_state
          != VSX_CONNECTION_RUNNING_STATE_DISCONNECTED);
}

static void
vsx_connection_class_init (VsxConnectionClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GParamSpec *pspec;

  gobject_class->dispose = vsx_connection_dispose;
  gobject_class->finalize = vsx_connection_finalize;
  gobject_class->constructed = vsx_connection_constructed;
  gobject_class->set_property = vsx_connection_set_property;
  gobject_class->get_property = vsx_connection_get_property;

  pspec = g_param_spec_object ("address",
                               "Server address to connect to",
                               "The address of the server to connect to. "
                               "Defaults to localhost:5144",
                               G_TYPE_SOCKET_ADDRESS,
                               G_PARAM_WRITABLE
                               | G_PARAM_CONSTRUCT_ONLY
                               | G_PARAM_STATIC_NAME
                               | G_PARAM_STATIC_NICK
                               | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (gobject_class, PROP_ADDRESS, pspec);

  pspec = g_param_spec_string ("room",
                               "Room to connect to",
                               "The name of the room to connect to",
                               "english",
                               G_PARAM_WRITABLE
                               | G_PARAM_CONSTRUCT_ONLY
                               | G_PARAM_STATIC_NAME
                               | G_PARAM_STATIC_NICK
                               | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (gobject_class, PROP_ROOM, pspec);

  pspec = g_param_spec_string ("player-name",
                               "Player name",
                               "Name of the player",
                               "player",
                               G_PARAM_WRITABLE
                               | G_PARAM_CONSTRUCT_ONLY
                               | G_PARAM_STATIC_NAME
                               | G_PARAM_STATIC_NICK
                               | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (gobject_class, PROP_PLAYER_NAME, pspec);

  pspec = g_param_spec_boolean ("running",
                                "Running",
                                "Whether the stream connection should be "
                                "trying to connect and receive objects",
                                false,
                                G_PARAM_READABLE
                                | G_PARAM_WRITABLE
                                | G_PARAM_STATIC_NAME
                                | G_PARAM_STATIC_NICK
                                | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (gobject_class, PROP_RUNNING, pspec);

  pspec = g_param_spec_boolean ("typing",
                                "Typing",
                                "Whether the user is typing.",
                                false,
                                G_PARAM_READABLE
                                | G_PARAM_STATIC_NAME
                                | G_PARAM_STATIC_NICK
                                | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (gobject_class, PROP_TYPING, pspec);

  pspec = g_param_spec_enum ("state",
                             "State",
                             "State of the conversation",
                             VSX_TYPE_CONNECTION_STATE,
                             VSX_CONNECTION_STATE_AWAITING_HEADER,
                             G_PARAM_READABLE
                             | G_PARAM_STATIC_NAME
                             | G_PARAM_STATIC_NICK
                             | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (gobject_class, PROP_STATE, pspec);

  signals[SIGNAL_GOT_ERROR] =
    g_signal_new ("got-error",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (VsxConnectionClass, got_error),
                  NULL, /* accumulator */
                  NULL, /* accumulator data */
                  vsx_marshal_VOID__BOXED,
                  G_TYPE_NONE,
                  1, /* num arguments */
                  G_TYPE_ERROR);

  signals[SIGNAL_MESSAGE] =
    g_signal_new ("message",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (VsxConnectionClass, message),
                  NULL, /* accumulator */
                  NULL, /* accumulator data */
                  vsx_marshal_VOID__POINTER_STRING,
                  G_TYPE_NONE,
                  2, /* num arguments */
                  G_TYPE_POINTER,
                  G_TYPE_STRING);

  signals[SIGNAL_PLAYER_CHANGED] =
    g_signal_new ("player-changed",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (VsxConnectionClass, player_changed),
                  NULL, /* accumulator */
                  NULL, /* accumulator data */
                  vsx_marshal_VOID__POINTER,
                  G_TYPE_NONE,
                  1, /* num arguments */
                  G_TYPE_POINTER);

  signals[SIGNAL_TILE_CHANGED] =
    g_signal_new ("tile-changed",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (VsxConnectionClass, tile_changed),
                  NULL, /* accumulator */
                  NULL, /* accumulator data */
                  vsx_marshal_VOID__BOOLEAN_POINTER,
                  G_TYPE_NONE,
                  2, /* num arguments */
                  G_TYPE_BOOLEAN,
                  G_TYPE_POINTER);

  signals[SIGNAL_PLAYER_SHOUTED] =
    g_signal_new ("player-shouted",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (VsxConnectionClass, player_shouted),
                  NULL, /* accumulator */
                  NULL, /* accumulator data */
                  vsx_marshal_VOID__POINTER,
                  G_TYPE_NONE,
                  1, /* num arguments */
                  G_TYPE_POINTER);
}

static void
free_player_cb (void *data)
{
  VsxPlayer *player = data;

  g_free (player->name);
  g_slice_free (VsxPlayer, player);
}

static void
free_tile_cb (void *data)
{
  VsxTile *tile = data;

  g_slice_free (VsxTile, tile);
}

static void
vsx_connection_init (VsxConnection *self)
{
  VsxConnectionPrivate *priv;

  priv = self->priv = vsx_connection_get_instance_private (self);

  priv->next_message_num = 0;

  priv->keep_alive_time = g_timer_new ();

  priv->players = g_hash_table_new_full (g_direct_hash,
                                         g_direct_equal,
                                         NULL, /* key_destroy */
                                         free_player_cb);
  priv->tiles = g_hash_table_new_full (g_direct_hash,
                                       g_direct_equal,
                                       NULL, /* key_destroy */
                                       free_tile_cb);

  vsx_list_init (&priv->tiles_to_move);
  vsx_list_init (&priv->messages_to_send);
}

static void
vsx_connection_dispose (GObject *object)
{
  VsxConnection *self = (VsxConnection *) object;
  VsxConnectionPrivate *priv = self->priv;

  vsx_connection_set_running_internal (self, false);

  if (priv->address)
    {
      g_object_unref (priv->address);
      priv->address = NULL;
    }

  G_OBJECT_CLASS (vsx_connection_parent_class)->dispose (object);
}

static void
free_tiles_to_move (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;
  VsxConnectionTileToMove *tile, *tmp;

  vsx_list_for_each_safe (tile, tmp, &priv->tiles_to_move, link)
    {
      g_free (tile);
    }
}

static void
free_messages_to_send (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;
  VsxConnectionMessageToSend *message, *tmp;

  vsx_list_for_each_safe (message, tmp, &priv->messages_to_send, link)
    {
      g_free (message);
    }
}

static void
vsx_connection_finalize (GObject *object)
{
  VsxConnection *self = (VsxConnection *) object;
  VsxConnectionPrivate *priv = self->priv;

  g_free (priv->room);
  g_free (priv->player_name);

  g_timer_destroy (priv->keep_alive_time);

  g_hash_table_destroy (priv->players);
  g_hash_table_destroy (priv->tiles);

  free_tiles_to_move (self);
  free_messages_to_send (self);

  G_OBJECT_CLASS (vsx_connection_parent_class)->finalize (object);
}

VsxConnection *
vsx_connection_new (GSocketAddress *address,
                    const char *room,
                    const char *player_name)
{
  VsxConnection *self = g_object_new (VSX_TYPE_CONNECTION,
                                      "address", address,
                                      "room", room,
                                      "player-name", player_name,
                                      NULL);

  return self;
}

bool
vsx_connection_get_typing (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  return priv->typing;
}

VsxConnectionState
vsx_connection_get_state (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  return priv->state;
}

void
vsx_connection_send_message (VsxConnection *connection,
                             const char *message)
{
  VsxConnectionPrivate *priv = connection->priv;

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

  vsx_list_insert (priv->messages_to_send.prev, &message_to_send->link);

  update_poll (connection);
}

void
vsx_connection_leave (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  priv->dirty_flags |= VSX_CONNECTION_DIRTY_FLAG_LEAVE;

  update_poll (connection);
}

const VsxPlayer *
vsx_connection_get_player (VsxConnection *connection,
                           int player_num)
{
  VsxConnectionPrivate *priv = connection->priv;

  return g_hash_table_lookup (priv->players,
                              GINT_TO_POINTER (player_num));
}

typedef struct
{
  VsxConnectionForeachPlayerCallback callback;
  void *user_data;
} ForeachPlayerData;

static void
foreach_player_cb (void *key,
                   void *value,
                   void *user_data)
{
  ForeachPlayerData *data = user_data;

  data->callback (value, data->user_data);
}

void
vsx_connection_foreach_player (VsxConnection *connection,
                               VsxConnectionForeachPlayerCallback callback,
                               void *user_data)
{
  VsxConnectionPrivate *priv = connection->priv;
  ForeachPlayerData data;

  data.callback = callback;
  data.user_data = user_data;

  g_hash_table_foreach (priv->players, foreach_player_cb, &data);
}

const VsxPlayer *
vsx_connection_get_self (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  return priv->self;
}

const VsxTile *
vsx_connection_get_tile (VsxConnection *connection,
                         int tile_num)
{
  VsxConnectionPrivate *priv = connection->priv;

  return g_hash_table_lookup (priv->tiles,
                              GINT_TO_POINTER (tile_num));
}

typedef struct
{
  VsxConnectionForeachTileCallback callback;
  void *user_data;
} ForeachTileData;

static void
foreach_tile_cb (void *key,
                 void *value,
                 void *user_data)
{
  ForeachTileData *data = user_data;

  data->callback (value, data->user_data);
}

void
vsx_connection_foreach_tile (VsxConnection *connection,
                             VsxConnectionForeachTileCallback callback,
                             void *user_data)
{
  VsxConnectionPrivate *priv = connection->priv;
  ForeachTileData data;

  data.callback = callback;
  data.user_data = user_data;

  g_hash_table_foreach (priv->tiles, foreach_tile_cb, &data);
}

GQuark
vsx_connection_error_quark (void)
{
  return g_quark_from_static_string ("vsx-connection-error-quark");
}
