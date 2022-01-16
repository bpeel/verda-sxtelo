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

#include "config.h"

#include "vsx-connection.h"

#include <inttypes.h>
#include <assert.h>
#include <string.h>

#include "vsx-ws-parser.h"
#include "vsx-proto.h"
#include "vsx-log.h"
#include "vsx-bitmask.h"
#include "vsx-normalize-name.h"
#include "vsx-base64.h"
#include "vsx-util.h"

typedef enum
{
  VSX_CONNECTION_STATE_READING_WS_HEADERS,
  VSX_CONNECTION_STATE_WRITING_DATA,
  VSX_CONNECTION_STATE_DONE,
} VsxConnectionState;

typedef enum
{
  VSX_CONNECTION_DIRTY_FLAG_WS_HEADER = (1 << 0),
  VSX_CONNECTION_DIRTY_FLAG_PONG = (1 << 1),
  VSX_CONNECTION_DIRTY_FLAG_PLAYER_ID = (1 << 2),
  VSX_CONNECTION_DIRTY_FLAG_CONVERSATION_ID = (1 << 3),
  VSX_CONNECTION_DIRTY_FLAG_N_TILES = (1 << 4),
  VSX_CONNECTION_DIRTY_FLAG_PENDING_SHOUT = (1 << 5),
  VSX_CONNECTION_DIRTY_FLAG_SYNC = (1 << 6),
  VSX_CONNECTION_DIRTY_FLAG_PENDING_ERROR = (1 << 7),
} VsxConnectionDirtyFlag;

struct _VsxConnection
{
  VsxConnectionState state;

  struct vsx_signal changed_signal;

  int64_t last_message_time;

  struct vsx_netaddress socket_address;
  VsxConversationSet *conversation_set;
  VsxPersonSet *person_set;

  /* This is freed and becomes NULL once the headers have all
   * been parsed.
   */
  VsxWsParser *ws_parser;

  VsxPerson *person;

  struct vsx_listener conversation_changed_listener;

  unsigned int message_num;

  /* Number of players that we've sent a "player-name" event for */
  unsigned int named_players;

  VsxConnectionDirtyFlag dirty_flags;

  /* Bit mask of players whose state needs updating */
  vsx_bitmask_element_t dirty_players
  [VSX_BITMASK_N_ELEMENTS_FOR_SIZE (VSX_CONVERSATION_MAX_PLAYERS)];

  /* Bit mask of tiles that need updating */
  vsx_bitmask_element_t dirty_tiles
  [VSX_BITMASK_N_ELEMENTS_FOR_SIZE (VSX_TILE_DATA_N_TILES)];

  int pending_shout;

  /* If DIRTY_FLAG_PENDING_ERROR is set, then a message with this
   * message number will be sent.
   */
  uint8_t pending_error;

  uint8_t read_buf[1024];
  size_t read_buf_pos;

  /* If VSX_CONNECTION_DIRTY_FLAG_PONG is set then we need to send a
   * pong control frame with the given payload.
   */
  _Static_assert (VSX_PROTO_MAX_CONTROL_FRAME_PAYLOAD <= UINT8_MAX,
                  "The max pong data length is too for a uint8_t");
  uint8_t pong_data_length;
  uint8_t pong_data[VSX_PROTO_MAX_CONTROL_FRAME_PAYLOAD];

  /* If message_data_length is non-zero then we are part way
   * through reading a message whose payload is stored in
   * message_data.
   */
  _Static_assert (VSX_PROTO_MAX_PAYLOAD_SIZE <= UINT16_MAX,
                  "The message size is too long for a uint16_t");
  uint16_t message_data_length;
  uint8_t message_data[VSX_PROTO_MAX_PAYLOAD_SIZE];
};

static const char
ws_header_prefix[] =
  "HTTP/1.1 101 Switching Protocols\r\n"
  "Upgrade: websocket\r\n"
  "Connection: Upgrade\r\n"
  "Sec-WebSocket-Accept: ";

static const char
ws_header_postfix[] = "\r\n\r\n";

struct vsx_error_domain
vsx_connection_error;

typedef int (* VsxConnectionWriteStateFunc) (VsxConnection *conn,
                                             uint8_t *buffer,
                                             size_t buffer_size);

static void
conversation_changed_cb (struct vsx_listener *listener,
                         void *user_data)
{
  VsxConnection *conn =
    vsx_container_of (listener, VsxConnection, conversation_changed_listener);
  VsxConversationChangedData *data = user_data;

  switch (data->type)
    {
    case VSX_CONVERSATION_N_TILES_CHANGED:
      conn->dirty_flags |= VSX_CONNECTION_DIRTY_FLAG_N_TILES;
      break;

    case VSX_CONVERSATION_PLAYER_CHANGED:
      vsx_bitmask_set (conn->dirty_players, data->num, true);
      break;

    case VSX_CONVERSATION_TILE_CHANGED:
      vsx_bitmask_set (conn->dirty_tiles, data->num, true);
      break;

    case VSX_CONVERSATION_STATE_CHANGED:
    case VSX_CONVERSATION_MESSAGE_ADDED:
      break;

    case VSX_CONVERSATION_SHOUTED:
      conn->pending_shout = data->num;
      conn->dirty_flags |= VSX_CONNECTION_DIRTY_FLAG_PENDING_SHOUT;
      break;
    }

  vsx_signal_emit (&conn->changed_signal, NULL);
}

static void
start_following_person (VsxConnection *conn)
{
  conn->dirty_flags |= (VSX_CONNECTION_DIRTY_FLAG_PLAYER_ID
                        | VSX_CONNECTION_DIRTY_FLAG_CONVERSATION_ID
                        | VSX_CONNECTION_DIRTY_FLAG_N_TILES
                        | VSX_CONNECTION_DIRTY_FLAG_SYNC);

  vsx_bitmask_set_range (conn->dirty_tiles,
                         conn->person->conversation->n_tiles_in_play);

  vsx_bitmask_set_range (conn->dirty_players,
                         conn->person->conversation->n_players);

  conn->conversation_changed_listener.notify = conversation_changed_cb;
  vsx_signal_add (&conn->person->conversation->changed_signal,
                  &conn->conversation_changed_listener);
}

static bool
handle_new_private_game (VsxConnection *conn,
                         struct vsx_error **error)
{
  const char *language_code, *player_name;

  if (!vsx_proto_read_payload (conn->message_data + 1,
                               conn->message_data_length - 1,

                               VSX_PROTO_TYPE_STRING,
                               &language_code,

                               VSX_PROTO_TYPE_STRING,
                               &player_name,

                               VSX_PROTO_TYPE_NONE))
    {
      vsx_set_error (error,
                     &vsx_connection_error,
                     VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                     "Invalid new private game command received");
      return false;
    }

  if (conn->person)
    {
      vsx_set_error (error,
                     &vsx_connection_error,
                     VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                     "Client sent a new private game request but already "
                     "specified a player");
      return false;
    }

  bool ret = true;
  char *normalized_player_name = vsx_strdup (player_name);

  if (!vsx_normalize_name (normalized_player_name))
    {
      vsx_set_error (error,
                     &vsx_connection_error,
                     VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                     "Client sent an invalid player name");
      ret = false;
    }
  else
    {
      VsxConversation *conversation =
        vsx_conversation_set_generate_conversation (conn->conversation_set,
                                                    language_code,
                                                    &conn->socket_address);

      conn->person = vsx_person_set_generate_person (conn->person_set,
                                                     player_name,
                                                     &conn->socket_address,
                                                     conversation);

      vsx_object_unref (conversation);

      vsx_log ("New player “%s” created private game %i",
               player_name,
               conversation->log_id);

      start_following_person (conn);
    }

  vsx_free (normalized_player_name);

  return ret;
}

static bool
handle_join_game (VsxConnection *conn,
                  struct vsx_error **error)
{
  uint64_t conversation_id;
  const char *player_name;

  if (!vsx_proto_read_payload (conn->message_data + 1,
                               conn->message_data_length - 1,

                               VSX_PROTO_TYPE_UINT64,
                               &conversation_id,

                               VSX_PROTO_TYPE_STRING,
                               &player_name,

                               VSX_PROTO_TYPE_NONE))
    {
      vsx_set_error (error,
                     &vsx_connection_error,
                     VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                     "Invalid join game command received");
      return false;
    }

  if (conn->person)
    {
      vsx_set_error (error,
                     &vsx_connection_error,
                     VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                     "Client sent a join game request but already "
                     "specified a player");
      return false;
    }

  VsxConversation *conversation =
    vsx_conversation_set_get_conversation (conn->conversation_set,
                                           conversation_id);

  bool ret = true;
  char *normalized_player_name = vsx_strdup (player_name);

  if (!vsx_normalize_name (normalized_player_name))
    {
      vsx_set_error (error,
                     &vsx_connection_error,
                     VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                     "Client sent an invalid player name");
      ret = false;
    }
  else if (conversation == NULL)
    {
      conn->pending_error = VSX_PROTO_BAD_CONVERSATION_ID;
      conn->dirty_flags |= VSX_CONNECTION_DIRTY_FLAG_PENDING_ERROR;
    }
  else
    {
      conn->person = vsx_person_set_generate_person (conn->person_set,
                                                     player_name,
                                                     &conn->socket_address,
                                                     conversation);

      vsx_log ("New player “%s” joined game %i",
               player_name,
               conversation->log_id);

      start_following_person (conn);
    }

  vsx_free (normalized_player_name);

  return ret;
}

static bool
handle_new_player (VsxConnection *conn,
                   struct vsx_error **error)
{
  const char *room_name, *player_name;

  if (!vsx_proto_read_payload (conn->message_data + 1,
                               conn->message_data_length - 1,

                               VSX_PROTO_TYPE_STRING,
                               &room_name,

                               VSX_PROTO_TYPE_STRING,
                               &player_name,

                               VSX_PROTO_TYPE_NONE))
    {
      vsx_set_error (error,
                     &vsx_connection_error,
                     VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                     "Invalid new player command received");
      return false;
    }

  if (conn->person)
    {
      vsx_set_error (error,
                     &vsx_connection_error,
                     VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                     "Client sent a new player request but already specified "
                     "a player");
      return false;
    }

  bool ret = true;
  char *normalized_room_name = vsx_strdup (room_name);
  char *normalized_player_name = vsx_strdup (player_name);

  if (!vsx_normalize_name (normalized_room_name))
    {
      vsx_set_error (error,
                     &vsx_connection_error,
                     VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                     "Client sent an invalid room name");
      ret = false;
    }
  else if (!vsx_normalize_name (normalized_player_name))
    {
      vsx_set_error (error,
                     &vsx_connection_error,
                     VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                     "Client sent an invalid player name");
      ret = false;
    }
  else
    {
      VsxConversation *conversation =
        vsx_conversation_set_get_pending_conversation (conn->conversation_set,
                                                       room_name,
                                                       &conn->socket_address);

      conn->person = vsx_person_set_generate_person (conn->person_set,
                                                     player_name,
                                                     &conn->socket_address,
                                                     conversation);

      vsx_object_unref (conversation);

      conn->message_num = conn->person->message_offset;

      if (conversation->n_players == 1)
        {
          vsx_log ("New player “%s” created game %i in “%s”",
                   player_name,
                   conversation->log_id,
                   room_name);
        }
      else
        {
          vsx_log ("New player “%s” joined game %i",
                   player_name,
                   conversation->log_id);
        }

      start_following_person (conn);
    }

  vsx_free (normalized_player_name);
  vsx_free (normalized_room_name);

  return ret;
}

static bool
handle_reconnect (VsxConnection *conn,
                  struct vsx_error **error)
{
  uint64_t player_id;
  uint16_t n_messages_received;

  if (!vsx_proto_read_payload (conn->message_data + 1,
                               conn->message_data_length - 1,

                               VSX_PROTO_TYPE_UINT64,
                               &player_id,

                               VSX_PROTO_TYPE_UINT16,
                               &n_messages_received,

                               VSX_PROTO_TYPE_NONE))
    {
      vsx_set_error (error,
                     &vsx_connection_error,
                     VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                     "Invalid reconnect command received");
      return false;
    }

  if (conn->person)
    {
      vsx_set_error (error,
                     &vsx_connection_error,
                     VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                     "Client sent a reconnect request but already specified "
                     "a player");
      return false;
    }

  VsxPerson *person = vsx_person_set_get_person (conn->person_set, player_id);

  if (person == NULL)
    {
      conn->pending_error = VSX_PROTO_BAD_PLAYER_ID;
      conn->dirty_flags |= VSX_CONNECTION_DIRTY_FLAG_PENDING_ERROR;
      return true;
    }

  int total_n_messages = vsx_conversation_get_n_messages (person->conversation);
  int n_messages_available = total_n_messages - person->message_offset;

  if (n_messages_received > n_messages_available)
    {
      vsx_set_error (error,
                     &vsx_connection_error,
                     VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                     "Client claimed to have received %i messages but only %i "
                     "are available",
                     n_messages_received,
                     n_messages_available);
      return false;
    }

  vsx_person_make_noise (person);
  conn->person = vsx_object_ref (person);
  conn->message_num = n_messages_received + person->message_offset;

  start_following_person (conn);

  return true;
}

static bool
activate_person (VsxConnection *conn,
                 struct vsx_error **error)
{
  if (conn->person == NULL)
    {
      vsx_set_error (error,
                     &vsx_connection_error,
                     VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                     "Client sent a command without a person");
      return false;
    }

  vsx_person_make_noise (conn->person);

  return true;
}

static bool
ensure_empty_payload (VsxConnection *conn,
                      const char *message_type,
                      struct vsx_error **error)
{
  if (conn->message_data_length != 1)
    {
      vsx_set_error (error,
                     &vsx_connection_error,
                     VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                     "Invalid %s message received",
                     message_type);
      return false;
    }

  return true;
}

static bool
handle_keep_alive (VsxConnection *conn,
                   struct vsx_error **error)
{
  if (!ensure_empty_payload (conn, "keep alive", error))
    return false;

  if (!activate_person (conn, error))
    return false;

  return true;
}

static bool
handle_leave (VsxConnection *conn,
              struct vsx_error **error)
{
  if (!ensure_empty_payload (conn, "leave", error))
    return false;

  if (!activate_person (conn, error))
    return false;

  vsx_person_leave_conversation (conn->person);

  return true;
}

static bool
handle_start_typing (VsxConnection *conn,
                     struct vsx_error **error)
{
  if (!ensure_empty_payload (conn, "start typing", error))
    return false;

  if (!activate_person (conn, error))
    return false;

  vsx_conversation_set_typing (conn->person->conversation,
                               conn->person->player->num,
                               true);

  return true;
}

static bool
handle_stop_typing (VsxConnection *conn,
                    struct vsx_error **error)
{
  if (!ensure_empty_payload (conn, "stop typing", error))
    return false;

  if (!activate_person (conn, error))
    return false;

  vsx_conversation_set_typing (conn->person->conversation,
                               conn->person->player->num,
                               false);

  return true;
}

static bool
handle_send_message (VsxConnection *conn,
                     struct vsx_error **error)
{
  const char *message;

  if (!vsx_proto_read_payload (conn->message_data + 1,
                               conn->message_data_length - 1,

                               VSX_PROTO_TYPE_STRING,
                               &message,

                               VSX_PROTO_TYPE_NONE))
    {
      vsx_set_error (error,
                     &vsx_connection_error,
                     VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                     "Invalid send message command received");
      return false;
    }

  if (!activate_person (conn, error))
    return false;

  vsx_conversation_add_message (conn->person->conversation,
                                conn->person->player->num,
                                message,
                                strlen (message));
  /* Sending a message implicitly marks the person as no longer
     typing */
  vsx_conversation_set_typing (conn->person->conversation,
                               conn->person->player->num,
                               false);

  return true;
}

static bool
handle_move_tile (VsxConnection *conn,
                  struct vsx_error **error)
{
  uint8_t tile_num;
  int16_t tile_x, tile_y;

  if (!vsx_proto_read_payload (conn->message_data + 1,
                               conn->message_data_length - 1,

                               VSX_PROTO_TYPE_UINT8,
                               &tile_num,

                               VSX_PROTO_TYPE_INT16,
                               &tile_x,

                               VSX_PROTO_TYPE_INT16,
                               &tile_y,

                               VSX_PROTO_TYPE_NONE))
    {
      vsx_set_error (error,
                     &vsx_connection_error,
                     VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                     "Invalid move tile command received");
      return false;
    }

  if (!activate_person (conn, error))
    return false;

  if (tile_num >= conn->person->conversation->n_tiles_in_play)
    {
      vsx_set_error (error,
                     &vsx_connection_error,
                     VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                     "Player tried to move a tile that is not in play");
      return false;
    }

  vsx_conversation_move_tile (conn->person->conversation,
                              conn->person->player->num,
                              tile_num,
                              tile_x,
                              tile_y);

  return true;
}

static bool
handle_turn (VsxConnection *conn,
             struct vsx_error **error)
{
  if (!ensure_empty_payload (conn, "turn", error))
    return false;

  if (!activate_person (conn, error))
    return false;

  vsx_conversation_turn (conn->person->conversation,
                         conn->person->player->num);

  return true;
}

static bool
handle_shout (VsxConnection *conn,
             struct vsx_error **error)
{
  if (!ensure_empty_payload (conn, "shout", error))
    return false;

  if (!activate_person (conn, error))
    return false;

  vsx_conversation_shout (conn->person->conversation,
                          conn->person->player->num);

  return true;
}

static bool
handle_set_n_tiles (VsxConnection *conn,
                    struct vsx_error **error)
{
  uint8_t n_tiles;

  if (!vsx_proto_read_payload (conn->message_data + 1,
                               conn->message_data_length - 1,

                               VSX_PROTO_TYPE_UINT8,
                               &n_tiles,

                               VSX_PROTO_TYPE_NONE))
    {
      vsx_set_error (error,
                     &vsx_connection_error,
                     VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                     "Invalid set_n_tiles command received");
      return false;
    }

  if (!activate_person (conn, error))
    return false;

  vsx_conversation_set_n_tiles (conn->person->conversation,
                                conn->person->player->num,
                                n_tiles);

  return true;
}

static bool
process_message (VsxConnection *conn,
                 struct vsx_error **error)
{
  if (conn->message_data_length < 1)
    {
      vsx_set_error (error,
                     &vsx_connection_error,
                     VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                     "Client sent an empty message");
      return false;
    }

  conn->last_message_time = vsx_main_context_get_monotonic_clock (NULL);

  switch (conn->message_data[0])
    {
    case VSX_PROTO_NEW_PRIVATE_GAME:
      return handle_new_private_game (conn, error);
    case VSX_PROTO_JOIN_GAME:
      return handle_join_game (conn, error);
    case VSX_PROTO_NEW_PLAYER:
      return handle_new_player (conn, error);
    case VSX_PROTO_RECONNECT:
      return handle_reconnect (conn, error);
    case VSX_PROTO_KEEP_ALIVE:
      return handle_keep_alive (conn, error);
    case VSX_PROTO_LEAVE:
      return handle_leave (conn, error);
    case VSX_PROTO_SEND_MESSAGE:
      return handle_send_message (conn, error);
    case VSX_PROTO_START_TYPING:
      return handle_start_typing (conn, error);
    case VSX_PROTO_STOP_TYPING:
      return handle_stop_typing (conn, error);
    case VSX_PROTO_TURN:
      return handle_turn (conn, error);
    case VSX_PROTO_MOVE_TILE:
      return handle_move_tile (conn, error);
    case VSX_PROTO_SHOUT:
      return handle_shout (conn, error);
    case VSX_PROTO_SET_N_TILES:
      return handle_set_n_tiles (conn, error);
    }

  vsx_set_error (error,
                 &vsx_connection_error,
                 VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                 "Client sent an unknown message ID (0x%x)",
                 conn->message_data[0]);

  return false;
}

VsxConnection *
vsx_connection_new (const struct vsx_netaddress *socket_address,
                    VsxConversationSet *conversation_set,
                    VsxPersonSet *person_set)
{
  VsxConnection *conn = vsx_calloc (sizeof *conn);

  conn->socket_address = *socket_address;
  conn->conversation_set = vsx_object_ref (conversation_set);
  conn->person_set = vsx_object_ref (person_set);

  conn->ws_parser = vsx_ws_parser_new ();

  conn->last_message_time = vsx_main_context_get_monotonic_clock (NULL);

  vsx_signal_init (&conn->changed_signal);

  return conn;
}

static bool
has_pending_data (VsxConnection *conn)
{
  if (conn->dirty_flags)
    return true;

  if (conn->person
      && conn->named_players < conn->person->conversation->n_players)
    return true;

  for (int i = 0; i < VSX_N_ELEMENTS (conn->dirty_players); i++)
    {
      if (conn->dirty_players[i])
        return true;
    }

  for (int i = 0; i < VSX_N_ELEMENTS (conn->dirty_tiles); i++)
    {
      if (conn->dirty_tiles[i])
        return true;
    }

  if (conn->person
      && (conn->message_num
          < vsx_conversation_get_n_messages (conn->person->conversation)))
    return true;

  return false;
}

static int
write_player_name (VsxConnection *conn,
                   uint8_t *buffer,
                   size_t buffer_size)
{
  /* This returns -1 if there wasn’t enough space, 0 if there are no
   * players to write or the size of the written data if one name was
   * written. We can only write one name at a time because there’s no
   * way to return that we wrote some data but still need to write
   * more.
   */

  if (conn->person == NULL)
    return 0;

  VsxConversation *conversation = conn->person->conversation;

  if (conn->named_players >= conversation->n_players)
    return 0;

  const VsxPlayer *player = conversation->players[conn->named_players];

  int wrote = vsx_proto_write_command (buffer,
                                       buffer_size,

                                       VSX_PROTO_PLAYER_NAME,

                                       VSX_PROTO_TYPE_UINT8,
                                       conn->named_players,

                                       VSX_PROTO_TYPE_STRING,
                                       player->name,

                                       VSX_PROTO_TYPE_NONE);

  if (wrote == -1)
    {
      return -1;
    }
  else
    {
      conn->named_players++;
      return wrote;
    }
}

static int
write_player (VsxConnection *conn,
              uint8_t *buffer,
              size_t buffer_size)
{
  /* This returns -1 if there wasn’t enough space, 0 if there are no
   * players to write or the size of the written data if one player was
   * written. We can only write one player at a time because there’s
   * no way to return that we wrote some data but still need to write
   * more.
   */

  for (int i = 0; i < VSX_N_ELEMENTS (conn->dirty_players); i++)
    {
      if (conn->dirty_players[i] == 0)
        continue;

      int bit_num = ffsl (conn->dirty_players[i]) - 1;
      int player_num = i * sizeof (unsigned long) * 8 + bit_num;

      const VsxPlayer *player = conn->person->conversation->players[player_num];

      int wrote = vsx_proto_write_command (buffer,
                                           buffer_size,

                                           VSX_PROTO_PLAYER,

                                           VSX_PROTO_TYPE_UINT8,
                                           player_num,

                                           VSX_PROTO_TYPE_UINT8,
                                           player->flags,

                                           VSX_PROTO_TYPE_NONE);

      if (wrote == -1)
        {
          return -1;
        }
      else
        {
          conn->dirty_players[i] &= ~(1UL << bit_num);
          return wrote;
        }
    }

  return 0;
}

static int
write_tile (VsxConnection *conn,
            uint8_t *buffer,
            size_t buffer_size)
{
  /* This returns -1 if there wasn’t enough space, 0 if there are no
   * tiles to write or the size of the written data if one tile was
   * written. We can only write one tile at a time because there’s no
   * way to return that we wrote some data but still need to write
   * more.
   */

  for (int i = 0; i < VSX_N_ELEMENTS (conn->dirty_tiles); i++)
    {
      if (conn->dirty_tiles[i] == 0)
        continue;

      int bit_num = ffsl (conn->dirty_tiles[i]) - 1;
      int tile_num = i * sizeof (unsigned long) * 8 + bit_num;

      const VsxTile *tile = conn->person->conversation->tiles + tile_num;

      int wrote = vsx_proto_write_command (buffer,
                                           buffer_size,

                                           VSX_PROTO_TILE,

                                           VSX_PROTO_TYPE_UINT8,
                                           tile_num,

                                           VSX_PROTO_TYPE_INT16,
                                           tile->x,

                                           VSX_PROTO_TYPE_INT16,
                                           tile->y,

                                           VSX_PROTO_TYPE_STRING,
                                           tile->letter,

                                           VSX_PROTO_TYPE_UINT8,
                                           tile->last_player,

                                           VSX_PROTO_TYPE_NONE);

      if (wrote == -1)
        {
          return -1;
        }
      else
        {
          conn->dirty_tiles[i] &= ~(1UL << bit_num);
          return wrote;
        }
    }

  return 0;
}

static int
write_message (VsxConnection *conn,
               uint8_t *buffer,
                   size_t buffer_size)
{
  /* This returns -1 if there wasn’t enough space, 0 if there are no
   * messages to write or the size of the written data if one message
   * was written. We can only write one message at a time because
   * there’s no way to return that we wrote some data but still need
   * to write more.
   */

  if (conn->person == NULL)
    return 0;

  VsxConversation *conversation = conn->person->conversation;

  if (conn->message_num
      >= vsx_conversation_get_n_messages (conversation))
    return 0;

  const VsxConversationMessage *message =
    vsx_conversation_get_message (conversation, conn->message_num);

  int wrote = vsx_proto_write_command (buffer,
                                       buffer_size,

                                       VSX_PROTO_MESSAGE,

                                       VSX_PROTO_TYPE_UINT8,
                                       message->player_num,

                                       VSX_PROTO_TYPE_STRING,
                                       message->text,

                                       VSX_PROTO_TYPE_NONE);

  if (wrote == -1)
    {
      return -1;
    }
  else
    {
      conn->message_num++;
      return wrote;
    }
}

static int
write_ws_response (VsxConnection *conn,
                   uint8_t *buffer,
                   size_t buffer_size)
{
  size_t key_hash_size;
  const uint8_t *key_hash = vsx_ws_parser_get_key_hash (conn->ws_parser,
                                                       &key_hash_size);

  size_t base64_size_needed = VSX_BASE64_ENCODED_SIZE (key_hash_size);

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

  uint8_t *p = buffer;

  memcpy (p, ws_header_prefix, (sizeof ws_header_prefix) - 1);
  p += (sizeof ws_header_prefix) - 1;

  size_t encoded_size = vsx_base64_encode (key_hash,
                                           key_hash_size,
                                           (char *) p);

  assert (encoded_size == base64_size_needed);

  p += base64_size_needed;

  memcpy (p, ws_header_postfix, (sizeof ws_header_postfix) - 1);
  p += (sizeof ws_header_postfix) - 1;

  return p - buffer;
}

static int
write_pong (VsxConnection *conn,
            uint8_t *buffer,
            size_t buffer_size)
{
  size_t frame_size = conn->pong_data_length + 2;

  if (frame_size > buffer_size)
    return -1;

  /* FIN bit + opcode 0xa (pong) */
  *(buffer++) = 0x8a;
  *(buffer++) = conn->pong_data_length;
  memcpy (buffer, conn->pong_data, conn->pong_data_length);

  return frame_size;
}

static int
write_player_id (VsxConnection *conn,
                 uint8_t *buffer,
                 size_t buffer_size)
{
  return vsx_proto_write_command (buffer,
                                  buffer_size,

                                  VSX_PROTO_PLAYER_ID,

                                  VSX_PROTO_TYPE_UINT64,
                                  conn->person->hash_entry.id,

                                  VSX_PROTO_TYPE_UINT8,
                                  conn->person->player->num,

                                  VSX_PROTO_TYPE_NONE);
}

static int
write_conversation_id (VsxConnection *conn,
                       uint8_t *buffer,
                       size_t buffer_size)
{
  return vsx_proto_write_command (buffer,
                                  buffer_size,

                                  VSX_PROTO_CONVERSATION_ID,

                                  VSX_PROTO_TYPE_UINT64,
                                  conn->person->conversation->hash_entry.id,

                                  VSX_PROTO_TYPE_NONE);
}

static int
write_n_tiles (VsxConnection *conn,
               uint8_t *buffer,
               size_t buffer_size)
{
  uint8_t n_tiles = conn->person->conversation->total_n_tiles;

  return vsx_proto_write_command (buffer,
                                  buffer_size,

                                  VSX_PROTO_N_TILES,

                                  VSX_PROTO_TYPE_UINT8,
                                  n_tiles,

                                  VSX_PROTO_TYPE_NONE);
}

static int
write_pending_shout (VsxConnection *conn,
                     uint8_t *buffer,
                     size_t buffer_size)
{
  return vsx_proto_write_command (buffer,
                                  buffer_size,

                                  VSX_PROTO_PLAYER_SHOUTED,

                                  VSX_PROTO_TYPE_UINT8,
                                  conn->pending_shout,

                                  VSX_PROTO_TYPE_NONE);
}

static int
write_end (VsxConnection *conn,
           uint8_t *buffer,
           size_t buffer_size)
{
  if (conn->person == NULL
      || vsx_player_is_connected (conn->person->player))
    return 0;

  int wrote = vsx_proto_write_command (buffer,
                                       buffer_size,

                                       VSX_PROTO_END,

                                       VSX_PROTO_TYPE_NONE);

  if (wrote != -1)
    conn->state = VSX_CONNECTION_STATE_DONE;

  return wrote;
}

static int
write_sync (VsxConnection *conn,
            uint8_t *buffer,
            size_t buffer_size)
{
  return vsx_proto_write_command (buffer,
                                  buffer_size,

                                  VSX_PROTO_SYNC,

                                  VSX_PROTO_TYPE_NONE);
}

static int
write_pending_error (VsxConnection *conn,
                     uint8_t *buffer,
                     size_t buffer_size)
{
  int wrote = vsx_proto_write_command (buffer,
                                       buffer_size,

                                       conn->pending_error,

                                       VSX_PROTO_TYPE_NONE);

  if (wrote != -1)
    conn->state = VSX_CONNECTION_STATE_DONE;

  return wrote;
}

size_t
vsx_connection_fill_output_buffer (VsxConnection *conn,
                                   uint8_t *buffer,
                                   size_t buffer_size)
{
  static const struct
  {
    VsxConnectionDirtyFlag flag;
    VsxConnectionWriteStateFunc func;
  } write_funcs[] =
    {
      { VSX_CONNECTION_DIRTY_FLAG_WS_HEADER, write_ws_response },
      { VSX_CONNECTION_DIRTY_FLAG_PONG, write_pong },
      { VSX_CONNECTION_DIRTY_FLAG_PLAYER_ID, write_player_id },
      { VSX_CONNECTION_DIRTY_FLAG_CONVERSATION_ID, write_conversation_id },
      { VSX_CONNECTION_DIRTY_FLAG_N_TILES, write_n_tiles },
      { .func = write_player_name },
      { .func = write_player },
      { VSX_CONNECTION_DIRTY_FLAG_PENDING_SHOUT, write_pending_shout },
      { .func = write_tile },
      { .func = write_message },
      { .func = write_end },
      { VSX_CONNECTION_DIRTY_FLAG_SYNC, write_sync },
      { VSX_CONNECTION_DIRTY_FLAG_PENDING_ERROR, write_pending_error },
    };

  size_t total_wrote = 0;

  while (true)
    {
      switch (conn->state)
        {
        case VSX_CONNECTION_STATE_READING_WS_HEADERS:
          return total_wrote;

        case VSX_CONNECTION_STATE_WRITING_DATA:
          for (int i = 0; i < VSX_N_ELEMENTS (write_funcs); i++)
            {
              if (write_funcs[i].flag == 0)
                {
                  int wrote = write_funcs[i].func (conn,
                                                   buffer + total_wrote,
                                                   buffer_size - total_wrote);

                  if (wrote == 0)
                    continue;

                  if (wrote == -1)
                    return total_wrote;

                  total_wrote += wrote;

                  goto found;
                }
              else
                {
                  if ((conn->dirty_flags & write_funcs[i].flag) == 0)
                    continue;

                  int wrote = write_funcs[i].func (conn,
                                                   buffer + total_wrote,
                                                   buffer_size - total_wrote);

                  if (wrote == -1)
                    return total_wrote;

                  total_wrote += wrote;

                  conn->dirty_flags &= ~write_funcs[i].flag;

                  goto found;
                }
            }

          return total_wrote;

        found:
          break;

        case VSX_CONNECTION_STATE_DONE:
          return total_wrote;
        }
    }
}

bool
vsx_connection_parse_eof (VsxConnection *conn,
                          struct vsx_error **error)
{
  if (conn->state == VSX_CONNECTION_STATE_READING_WS_HEADERS)
    {
      vsx_set_error (error,
                     &vsx_connection_error,
                     VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                     "Client closed the connection before finishing WebSocket "
                     "negotiation");
      return false;
    }

  if (conn->read_buf_pos > 0 || conn->message_data_length > 0)
    {
      vsx_set_error (error,
                     &vsx_connection_error,
                     VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                     "Client closed the connection in the middle of a frame");
      return false;
    }

  /* The player shouldn’t close the connection without leaving the
   * game. If they do leave the game first this will initiate a clean
   * shutdown sequence because the state will be changed to DONE when
   * the END command gets sent.
   */
  if (!vsx_connection_is_finished (conn)
      && (conn->person == NULL
          || vsx_player_is_connected (conn->person->player)))
    {
      vsx_set_error (error,
                     &vsx_connection_error,
                     VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                     "Client closed the connection before sending a LEAVE "
                     "command");
      return false;
    }

  return true;
}

static bool
process_control_frame (VsxConnection *conn,
                       int opcode,
                       const uint8_t *data,
                       size_t data_length,
                       struct vsx_error **error)
{
  switch (opcode)
    {
    case 0x8:
      /* Close control frame, ignore */
      return true;
    case 0x9:
      assert (data_length <= sizeof conn->pong_data);
      memcpy (conn->pong_data, data, data_length);
      conn->pong_data_length = data_length;
      conn->dirty_flags |= VSX_CONNECTION_DIRTY_FLAG_PONG;
      break;
    case 0xa:
      /* pong, ignore */
      break;
    default:
      vsx_set_error (error,
                     &vsx_connection_error,
                     VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                     "Client sent an unknown control frame");
    return false;
  }

  return true;
}

static void
unmask_data (uint32_t mask,
             uint8_t *buffer,
             size_t buffer_length)
{
  int i;

  for (i = 0; i + sizeof mask <= buffer_length; i += sizeof mask)
    {
      uint32_t val;

      memcpy (&val, buffer + i, sizeof val);
      val ^= mask;
      memcpy (buffer + i, &val, sizeof val);
    }

  for (; i < buffer_length; i++)
    buffer[i] ^= ((uint8_t *) &mask)[i % 4];
}

static bool
process_frames (VsxConnection *conn,
                struct vsx_error **error)
{
  uint8_t *data = conn->read_buf;
  size_t length = conn->read_buf_pos;
  bool has_mask;
  bool is_fin;
  uint32_t mask;
  uint64_t payload_length;
  uint8_t opcode;

  while (true)
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
          uint16_t word;
          if (length < header_size + sizeof word)
            break;
          memcpy (&word, data + header_size, sizeof word);
          payload_length = VSX_UINT16_FROM_BE (word);
          header_size += sizeof word;
        }
      else if (payload_length == 127)
        {
          if (length < header_size + sizeof payload_length)
            break;
          memcpy (&payload_length,
                  data + header_size,
                  sizeof payload_length);
          payload_length = VSX_UINT64_FROM_BE (payload_length);
          header_size += sizeof payload_length;
        }

      if (has_mask)
        header_size += sizeof mask;

      /* RSV bits must be zero */
      if (data[0] & 0x70)
        {
          vsx_set_error (error,
                         &vsx_connection_error,
                         VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                         "Client sent a frame with non-zero RSV bits");
          return false;
        }

      if (opcode & 0x8)
        {
          /* Control frame */
          if (payload_length > VSX_PROTO_MAX_CONTROL_FRAME_PAYLOAD)
            {
              vsx_set_error (error,
                             &vsx_connection_error,
                             VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                             "Client sent a control frame (0x%x) "
                             "that is too long (%" PRIu64 ")",
                             opcode,
                             payload_length);
              return false;
            }
          if (!is_fin)
            {
              vsx_set_error (error,
                             &vsx_connection_error,
                             VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                             "Client sent a fragmented "
                             "control frame");
              return false;
            }
        }
      else if (opcode == 0x2 || opcode == 0x0)
        {
          if (payload_length + conn->message_data_length
              > VSX_PROTO_MAX_PAYLOAD_SIZE)
            {
              vsx_set_error (error,
                             &vsx_connection_error,
                             VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                             "Client sent a message (0x%x) "
                             "that is too long (%" PRIu64 ")",
                             opcode,
                             payload_length);
              return false;
            }
          if (opcode == 0x0 && conn->message_data_length == 0)
            {
              vsx_set_error (error,
                             &vsx_connection_error,
                             VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                             "Client sent a continuation frame "
                             "without starting a message");
              return false;
            }
          if (payload_length == 0 && !is_fin)
            {
              vsx_set_error (error,
                             &vsx_connection_error,
                             VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                             "Client sent an empty fragmented "
                             "message");
              return false;
            }
        }
      else
        {
          vsx_set_error (error,
                         &vsx_connection_error,
                         VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
                         "Client sent a frame opcode (0x%x) which "
                         "the server doesn’t understand",
                         opcode);
          return false;
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
            return false;
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
                return false;

              conn->message_data_length = 0;
            }
        }

      data += payload_length;
      length -= payload_length;
    }

  memmove (conn->read_buf, data, length);
  conn->read_buf_pos = length;

  return true;
}

bool
vsx_connection_parse_data (VsxConnection *conn,
                           const uint8_t *buffer,
                           size_t buffer_length,
                           struct vsx_error **error)
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
          return true;
        case VSX_WS_PARSER_RESULT_ERROR:
          return false;
        case VSX_WS_PARSER_RESULT_FINISHED:
          conn->state = VSX_CONNECTION_STATE_WRITING_DATA;
          conn->dirty_flags |= VSX_CONNECTION_DIRTY_FLAG_WS_HEADER;
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
        return false;
    }

  return true;
}

bool
vsx_connection_is_finished (VsxConnection *conn)
{
  return conn->state == VSX_CONNECTION_STATE_DONE;
}

bool
vsx_connection_has_data (VsxConnection *conn)
{
  switch (conn->state)
    {
    case VSX_CONNECTION_STATE_READING_WS_HEADERS:
      return false;
    case VSX_CONNECTION_STATE_WRITING_DATA:
      return has_pending_data (conn);
    case VSX_CONNECTION_STATE_DONE:
      return false;
    }

  assert (!"Invalid connection state encountered");

  return false;
}

struct vsx_signal *
vsx_connection_get_changed_signal (VsxConnection *conn)
{
  return &conn->changed_signal;
}

int64_t
vsx_connection_get_last_message_time (VsxConnection *conn)
{
  return conn->last_message_time;
}

void
vsx_connection_free (VsxConnection *conn)
{
  if (conn->person)
    {
      vsx_list_remove (&conn->conversation_changed_listener.link);
      vsx_object_unref (conn->person);
    }

  vsx_object_unref (conn->conversation_set);
  vsx_object_unref (conn->person_set);

  if (conn->ws_parser)
    vsx_ws_parser_free (conn->ws_parser);

  vsx_free (conn);
}
