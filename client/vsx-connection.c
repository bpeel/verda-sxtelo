/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2012, 2013, 2021, 2022  Neil Roberts
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
#include <assert.h>
#include <stdalign.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>

#include "vsx-proto.h"
#include "vsx-list.h"
#include "vsx-util.h"
#include "vsx-buffer.h"
#include "vsx-slab.h"
#include "vsx-utf8.h"
#include "vsx-netaddress.h"
#include "vsx-socket.h"
#include "vsx-error.h"
#include "vsx-monotonic.h"
#include "vsx-file-error.h"

static const uint8_t
ws_terminator[] = "\r\n\r\n";

#define WS_TERMINATOR_LENGTH ((sizeof ws_terminator) - 1)

enum vsx_connection_dirty_flag {
        VSX_CONNECTION_DIRTY_FLAG_WS_HEADER = (1 << 0),
        VSX_CONNECTION_DIRTY_FLAG_HEADER = (1 << 1),
        VSX_CONNECTION_DIRTY_FLAG_KEEP_ALIVE = (1 << 2),
        VSX_CONNECTION_DIRTY_FLAG_LEAVE = (1 << 3),
        VSX_CONNECTION_DIRTY_FLAG_SHOUT = (1 << 4),
        VSX_CONNECTION_DIRTY_FLAG_TURN = (1 << 5),
        VSX_CONNECTION_DIRTY_FLAG_N_TILES = (1 << 6),
        VSX_CONNECTION_DIRTY_FLAG_LANGUAGE = (1 << 7),
};

typedef int
(* vsx_connection_write_state_func)(struct vsx_connection *conn,
                                    uint8_t *buffer,
                                    size_t buffer_size);

struct vsx_error_domain
vsx_connection_error;

static void
update_poll(struct vsx_connection *connection);

/* Initial timeout (in microseconds) before attempting to reconnect
 * after an error. The timeout will be doubled every time there is a
 * failure
 */
#define VSX_CONNECTION_INITIAL_TIMEOUT (16 * 1000 * 1000)

/* If the timeout reaches this maximum then it won't be doubled further */
#define VSX_CONNECTION_MAX_TIMEOUT (512 * 1000 * 1000)

/* Time in microseconds after the last message before sending a keep
 * alive message (2.5 minutes)
 */
#define VSX_CONNECTION_KEEP_ALIVE_TIME (150 * 1000 * 1000)

/* If the connection stays alive for at least this much time after
 * receiving the player_id command then we will assume it’s stable and
 * reconnect immediately if an error occurs.
 */
#define VSX_CONNECTION_STABLE_TIME (15 * 1000 * 1000)

enum vsx_connection_running_state {
        VSX_CONNECTION_RUNNING_STATE_DISCONNECTED,
        /* connect has been called and we are waiting for it to
         * become ready for writing
         */
        VSX_CONNECTION_RUNNING_STATE_RECONNECTING,
        VSX_CONNECTION_RUNNING_STATE_RUNNING,
        VSX_CONNECTION_RUNNING_STATE_WAITING_FOR_RECONNECT,
        /* The connection is missing some configuration properties
         * that are needed before connecting, such as the address or a
         * player name. Once these are set by the higher layers it
         * will automatically start trying to connect.
         */
        VSX_CONNECTION_RUNNING_STATE_WAITING_FOR_CONFIGURATION,
};

struct vsx_connection_tile_to_move {
        int num;
        int x;
        int y;
        struct vsx_list link;
};

struct vsx_connection_message_to_send {
        struct vsx_list link;
        /* Over-allocated */
        char message[1];
};

struct vsx_connection {
        bool has_address;
        struct vsx_netaddress address;

        bool has_conversation_id;
        uint64_t conversation_id;

        char *room;
        char *player_name;
        bool has_person_id;
        uint64_t person_id;
        enum vsx_connection_running_state running_state;
        bool finished;
        bool typing;
        bool sent_typing_state;
        bool write_finished;
        int next_message_num;

        /* Delay in microseconds that the next reconnect timeout will be
         * scheduled for.
         */
        unsigned reconnect_timeout;
        /* Monotonic time to wake up for reconnection at, or INT64_MAX if no
         * reconnect is scheduled.
         */
        int64_t reconnect_timestamp;
        /* The last time we got a player_id command in this
         * connection, or INT64_MAX if we haven’t yet. This is used to
         * reset the reconnect timeout if the connection was stable
         * for a while.
         */
        int64_t player_id_received_timestamp;

        struct vsx_signal event_signal;

        enum vsx_connection_dirty_flag dirty_flags;
        struct vsx_list tiles_to_move;
        struct vsx_list messages_to_send;
        /* The n_tiles value that is queued to send to the server */
        int n_tiles_to_send;
        /* The language code that is queued to send to the server */
        char language_to_send[8];

        int sock;

        /* The last poll_changed event that we sent so we can detect
         * changes.
         */
        struct vsx_connection_event poll_changed_event;

        /* true if we have received a sync message since the last time
         * we reconnected.
         */
        bool synced;

        /* Monotonic time to wake up at in order to send a keepalive, or
         * INT64_MAX if no keepalive is scheduled.
         */
        int64_t keep_alive_timestamp;

        unsigned int output_length;
        uint8_t output_buffer[VSX_PROTO_MAX_PAYLOAD_SIZE +
                              VSX_PROTO_MAX_FRAME_HEADER_LENGTH];

        unsigned int input_length;
        uint8_t input_buffer[VSX_PROTO_MAX_PAYLOAD_SIZE +
                             VSX_PROTO_MAX_FRAME_HEADER_LENGTH];

        /* Position within ws terminator that we have found so far. If this
         * is greater than the terminator length then we’ve finished the
         * WebSocket negotation.
         */
        unsigned int ws_terminator_pos;
};

static void
emit_event(struct vsx_connection *connection,
           struct vsx_connection_event *event)
{
        event->synced = connection->synced;

        vsx_signal_emit(&connection->event_signal, event);
}

static void
vsx_connection_signal_error(struct vsx_connection *connection,
                            struct vsx_error *error)
{
        struct vsx_connection_event event = {
                .type = VSX_CONNECTION_EVENT_TYPE_ERROR,
                .error = { .error = error },
        };

        emit_event(connection, &event);
}

static void
close_socket(struct vsx_connection *connection)
{
        if (connection->sock != -1) {
                vsx_close(connection->sock);
                connection->sock = -1;
                connection->keep_alive_timestamp = INT64_MAX;
        }
}

void
vsx_connection_set_typing(struct vsx_connection *connection,
                          bool typing)
{
        if (connection->typing != typing) {
                connection->typing = typing;
                update_poll(connection);
        }
}

void
vsx_connection_shout(struct vsx_connection *connection)
{
        connection->dirty_flags |= VSX_CONNECTION_DIRTY_FLAG_SHOUT;

        update_poll(connection);
}

void
vsx_connection_turn(struct vsx_connection *connection)
{
        connection->dirty_flags |= VSX_CONNECTION_DIRTY_FLAG_TURN;

        update_poll(connection);
}

void
vsx_connection_move_tile(struct vsx_connection *connection,
                         int tile_num,
                         int x, int y)
{
        struct vsx_connection_tile_to_move *tile;

        vsx_list_for_each(tile, &connection->tiles_to_move, link) {
                if (tile->num == tile_num)
                        goto found_tile;
        }

        tile = vsx_alloc(sizeof *tile);
        tile->num = tile_num;
        vsx_list_insert(connection->tiles_to_move.prev, &tile->link);

found_tile:
        tile->x = x;
        tile->y = y;

        update_poll(connection);
}

void
vsx_connection_set_n_tiles(struct vsx_connection *connection,
                           int n_tiles)
{
        connection->n_tiles_to_send = n_tiles;
        connection->dirty_flags |= VSX_CONNECTION_DIRTY_FLAG_N_TILES;

        update_poll(connection);
}

void
vsx_connection_set_default_language(struct vsx_connection *connection,
                                    const char *language_code)
{
        size_t max_len = VSX_N_ELEMENTS(connection->language_to_send) - 1;

        strncpy(connection->language_to_send, language_code, max_len);
        connection->language_to_send[max_len] = '\0';
}

void
vsx_connection_set_language(struct vsx_connection *connection,
                            const char *language_code)
{
        vsx_connection_set_default_language(connection, language_code);

        connection->dirty_flags |= VSX_CONNECTION_DIRTY_FLAG_LANGUAGE;

        update_poll(connection);
}

static bool
handle_player_id(struct vsx_connection *connection,
                 const uint8_t *payload,
                 size_t payload_length,
                 struct vsx_error **error)
{
        uint8_t self_num;

        if (!vsx_proto_read_payload(payload + 1,
                                    payload_length - 1,

                                    VSX_PROTO_TYPE_UINT64,
                                    &connection->person_id,

                                    VSX_PROTO_TYPE_UINT8,
                                    &self_num,

                                    VSX_PROTO_TYPE_NONE)) {
                vsx_set_error(error,
                              &vsx_connection_error,
                              VSX_CONNECTION_ERROR_BAD_DATA,
                              "The server sent an invalid player_id command");
                return false;
        }

        connection->has_person_id = true;
        connection->player_id_received_timestamp = vsx_monotonic_get();

        struct vsx_connection_event event = {
                .type = VSX_CONNECTION_EVENT_TYPE_HEADER,
                .header = {
                        .self_num = self_num,
                        .person_id = connection->person_id,
                },
        };

        emit_event(connection, &event);

        return true;
}

static bool
handle_conversation_id(struct vsx_connection *connection,
                       const uint8_t *payload,
                       size_t payload_length,
                       struct vsx_error **error)
{
        uint64_t id;

        if (!vsx_proto_read_payload(payload + 1,
                                    payload_length - 1,

                                    VSX_PROTO_TYPE_UINT64,
                                    &id,

                                    VSX_PROTO_TYPE_NONE)) {
                vsx_set_error(error,
                              &vsx_connection_error,
                              VSX_CONNECTION_ERROR_BAD_DATA,
                              "The server sent an invalid conversation_id "
                              "command");
                return false;
        }

        struct vsx_connection_event event = {
                .type = VSX_CONNECTION_EVENT_TYPE_CONVERSATION_ID,
                .conversation_id = {
                        .id = id,
                },
        };

        emit_event(connection, &event);

        return true;
}

static bool
handle_n_tiles(struct vsx_connection *connection,
               const uint8_t *payload,
               size_t payload_length,
               struct vsx_error **error)
{
        uint8_t n_tiles;

        if (!vsx_proto_read_payload(payload + 1,
                                    payload_length - 1,

                                    VSX_PROTO_TYPE_UINT8,
                                    &n_tiles,

                                    VSX_PROTO_TYPE_NONE)) {
                vsx_set_error(error,
                              &vsx_connection_error,
                              VSX_CONNECTION_ERROR_BAD_DATA,
                              "The server sent an invalid n_tiles command");
                return false;
        }

        struct vsx_connection_event event = {
                .type = VSX_CONNECTION_EVENT_TYPE_N_TILES_CHANGED,
                .n_tiles_changed = {
                        .n_tiles = n_tiles,
                },
        };

        emit_event(connection, &event);

        return true;
}

static bool
handle_language(struct vsx_connection *connection,
                const uint8_t *payload,
                size_t payload_length,
                struct vsx_error **error)
{
        const char *language_code;

        if (!vsx_proto_read_payload(payload + 1,
                                    payload_length - 1,

                                    VSX_PROTO_TYPE_STRING,
                                    &language_code,

                                    VSX_PROTO_TYPE_NONE)) {
                vsx_set_error(error,
                              &vsx_connection_error,
                              VSX_CONNECTION_ERROR_BAD_DATA,
                              "The server sent an invalid language command");
                return false;
        }

        struct vsx_connection_event event = {
                .type = VSX_CONNECTION_EVENT_TYPE_LANGUAGE_CHANGED,
                .language_changed = {
                        .code = language_code,
                },
        };

        emit_event(connection, &event);

        return true;
}

static bool
handle_message(struct vsx_connection *connection,
               const uint8_t *payload,
               size_t payload_length,
               struct vsx_error **error)
{
        uint8_t person;
        const char *text;

        if (!vsx_proto_read_payload(payload + 1,
                                    payload_length - 1,

                                    VSX_PROTO_TYPE_UINT8,
                                    &person,

                                    VSX_PROTO_TYPE_STRING,
                                    &text,

                                    VSX_PROTO_TYPE_NONE)) {
                vsx_set_error(error,
                              &vsx_connection_error,
                              VSX_CONNECTION_ERROR_BAD_DATA,
                              "The server sent an invalid message command");
                return false;
        }

        connection->next_message_num++;

        struct vsx_connection_event event = {
                .type = VSX_CONNECTION_EVENT_TYPE_MESSAGE,
                .message = {
                        .player_num = person,
                        .message = text,
                },
        };

        emit_event(connection, &event);

        return true;
}

static bool
handle_tile(struct vsx_connection *connection,
            const uint8_t *payload,
            size_t payload_length,
            struct vsx_error **error)
{
        uint8_t num, player;
        int16_t x, y;
        const char *letter;

        if (!vsx_proto_read_payload(payload + 1,
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

                                    VSX_PROTO_TYPE_NONE) ||
            *letter == 0 ||
            *vsx_utf8_next(letter) != 0) {
                vsx_set_error(error,
                              &vsx_connection_error,
                              VSX_CONNECTION_ERROR_BAD_DATA,
                              "The server sent an invalid tile command");
                return false;
        }

        struct vsx_connection_event event = {
                .type = VSX_CONNECTION_EVENT_TYPE_TILE_CHANGED,
                .tile_changed = {
                        .num = num,
                        .last_player_moved = player,
                        .x = x,
                        .y = y,
                        .letter = vsx_utf8_get_char(letter),
                },
        };

        emit_event(connection, &event);

        return true;
}

static bool
handle_player_name(struct vsx_connection *connection,
                   const uint8_t *payload,
                   size_t payload_length,
                   struct vsx_error **error)
{
        uint8_t num;
        const char *name;

        if (!vsx_proto_read_payload(payload + 1,
                                    payload_length - 1,

                                    VSX_PROTO_TYPE_UINT8,
                                    &num,

                                    VSX_PROTO_TYPE_STRING,
                                    &name,

                                    VSX_PROTO_TYPE_NONE)) {
                vsx_set_error(error,
                              &vsx_connection_error,
                              VSX_CONNECTION_ERROR_BAD_DATA,
                              "The server sent an invalid player_name command");
                return false;
        }

        struct vsx_connection_event event = {
                .type = VSX_CONNECTION_EVENT_TYPE_PLAYER_NAME_CHANGED,
                .player_name_changed = {
                        .player_num = num,
                        .name = name,
                },
        };

        emit_event(connection, &event);

        return true;
}

static bool
handle_player(struct vsx_connection *connection,
              const uint8_t *payload,
              size_t payload_length, struct vsx_error **error)
{
        uint8_t num, flags;

        if (!vsx_proto_read_payload(payload + 1,
                                    payload_length - 1,

                                    VSX_PROTO_TYPE_UINT8,
                                    &num,

                                    VSX_PROTO_TYPE_UINT8,
                                    &flags,

                                    VSX_PROTO_TYPE_NONE)) {
                vsx_set_error(error,
                              &vsx_connection_error,
                              VSX_CONNECTION_ERROR_BAD_DATA,
                              "The server sent an invalid player command");
                return false;
        }

        struct vsx_connection_event event = {
                .type = VSX_CONNECTION_EVENT_TYPE_PLAYER_FLAGS_CHANGED,
                .player_flags_changed = {
                        .player_num = num,
                        .flags = flags,
                },
        };

        emit_event(connection, &event);

        return true;
}

static bool
handle_player_shouted(struct vsx_connection *connection,
                      const uint8_t *payload,
                      size_t payload_length, struct vsx_error **error)
{
        uint8_t player_num;

        if (!vsx_proto_read_payload(payload + 1,
                                    payload_length - 1,

                                    VSX_PROTO_TYPE_UINT8,
                                    &player_num,

                                    VSX_PROTO_TYPE_NONE)) {
                vsx_set_error(error,
                              &vsx_connection_error,
                              VSX_CONNECTION_ERROR_BAD_DATA,
                              "The server sent an invalid player_shouted "
                              "command");
                return false;
        }

        struct vsx_connection_event event = {
                .type = VSX_CONNECTION_EVENT_TYPE_PLAYER_SHOUTED,
                .player_shouted = {
                        .player_num = player_num,
                },
        };

        emit_event(connection, &event);

        return true;
}

static bool
handle_sync(struct vsx_connection *connection,
            const uint8_t *payload,
            size_t payload_length, struct vsx_error **error)
{
        if (!vsx_proto_read_payload(payload + 1,
                                    payload_length - 1,

                                    VSX_PROTO_TYPE_NONE)) {
                vsx_set_error(error,
                              &vsx_connection_error,
                              VSX_CONNECTION_ERROR_BAD_DATA,
                              "The server sent an invalid sync command");
                return false;
        }

        connection->synced = true;

        return true;
}

static bool
handle_end(struct vsx_connection *connection,
           const uint8_t *payload,
           size_t payload_length, struct vsx_error **error)
{
        if (!vsx_proto_read_payload(payload + 1,
                                    payload_length - 1,

                                    VSX_PROTO_TYPE_NONE)) {
                vsx_set_error(error,
                              &vsx_connection_error,
                              VSX_CONNECTION_ERROR_BAD_DATA,
                              "The server sent an invalid end command");
                return false;
        }

        connection->finished = true;

        struct vsx_connection_event event = {
                .type = VSX_CONNECTION_EVENT_TYPE_END,
        };

        emit_event(connection, &event);

        return true;
}

static bool
handle_bad_player_id(struct vsx_connection *connection,
                     const uint8_t *payload,
                     size_t payload_length,
                     struct vsx_error **error)
{
        if (!vsx_proto_read_payload(payload + 1,
                                    payload_length - 1,

                                    VSX_PROTO_TYPE_NONE)) {
                vsx_set_error(error,
                              &vsx_connection_error,
                              VSX_CONNECTION_ERROR_BAD_DATA,
                              "The server sent an invalid bad player ID "
                              "command");
                return false;
        }

        connection->finished = true;

        /* This error is emitted like this because we don’t want to
         * try to reconnect. Instead it should try to shutdown
         * gracefully.
         */

        struct vsx_error *bad_error = NULL;

        vsx_set_error(&bad_error,
                      &vsx_connection_error,
                      VSX_CONNECTION_ERROR_BAD_PLAYER_ID,
                      "The player ID no longer exists");

        vsx_connection_signal_error(connection, bad_error);

        vsx_error_free(bad_error);

        return true;
}

static bool
handle_bad_conversation_id(struct vsx_connection *connection,
                           const uint8_t *payload,
                           size_t payload_length,
                           struct vsx_error **error)
{
        if (!vsx_proto_read_payload(payload + 1,
                                    payload_length - 1,

                                    VSX_PROTO_TYPE_NONE)) {
                vsx_set_error(error,
                              &vsx_connection_error,
                              VSX_CONNECTION_ERROR_BAD_DATA,
                              "The server sent an invalid bad conversation ID "
                              "command");
                return false;
        }

        connection->finished = true;

        /* This error is emitted like this because we don’t want to
         * try to reconnect. Instead it should try to shutdown
         * gracefully.
         */

        struct vsx_error *bad_error = NULL;

        vsx_set_error(&bad_error,
                      &vsx_connection_error,
                      VSX_CONNECTION_ERROR_BAD_CONVERSATION_ID,
                      "The conversation ID no longer exists");

        vsx_connection_signal_error(connection, bad_error);

        vsx_error_free(bad_error);

        return true;
}

static bool
handle_conversation_full(struct vsx_connection *connection,
                         const uint8_t *payload,
                         size_t payload_length,
                         struct vsx_error **error)
{
        if (!vsx_proto_read_payload(payload + 1,
                                    payload_length - 1,

                                    VSX_PROTO_TYPE_NONE)) {
                vsx_set_error(error,
                              &vsx_connection_error,
                              VSX_CONNECTION_ERROR_BAD_DATA,
                              "The server sent an invalid conversation full "
                              "command");
                return false;
        }

        connection->finished = true;

        /* This error is emitted like this because we don’t want to
         * try to reconnect. Instead it should try to shutdown
         * gracefully.
         */

        struct vsx_error *bad_error = NULL;

        vsx_set_error(&bad_error,
                      &vsx_connection_error,
                      VSX_CONNECTION_ERROR_CONVERSATION_FULL,
                      "The conversation is full");

        vsx_connection_signal_error(connection, bad_error);

        vsx_error_free(bad_error);

        return true;
}

static bool
process_message(struct vsx_connection *connection,
                const uint8_t *payload,
                size_t payload_length, struct vsx_error **error)
{
        if (payload_length < 1) {
                vsx_set_error(error,
                              &vsx_connection_error,
                              VSX_CONNECTION_ERROR_BAD_DATA,
                              "The server sent an empty message");
                return false;
        }

        switch (payload[0]) {
        case VSX_PROTO_PLAYER_ID:
                return handle_player_id(connection,
                                        payload, payload_length,
                                        error);
        case VSX_PROTO_CONVERSATION_ID:
                return handle_conversation_id(connection,
                                              payload, payload_length,
                                              error);
        case VSX_PROTO_N_TILES:
                return handle_n_tiles(connection,
                                      payload, payload_length,
                                      error);
        case VSX_PROTO_LANGUAGE:
                return handle_language(connection,
                                       payload, payload_length,
                                       error);
        case VSX_PROTO_MESSAGE:
                return handle_message(connection,
                                      payload, payload_length,
                                      error);
        case VSX_PROTO_TILE:
                return handle_tile(connection,
                                   payload, payload_length,
                                   error);
        case VSX_PROTO_PLAYER_NAME:
                return handle_player_name(connection,
                                          payload, payload_length,
                                          error);
        case VSX_PROTO_PLAYER:
                return handle_player(connection,
                                     payload, payload_length,
                                     error);
        case VSX_PROTO_PLAYER_SHOUTED:
                return handle_player_shouted(connection,
                                             payload, payload_length,
                                             error);
        case VSX_PROTO_SYNC:
                return handle_sync(connection,
                                  payload, payload_length,
                                  error);
        case VSX_PROTO_END:
                return handle_end(connection,
                                  payload, payload_length,
                                  error);
        case VSX_PROTO_BAD_PLAYER_ID:
                return handle_bad_player_id(connection,
                                            payload, payload_length,
                                            error);
        case VSX_PROTO_BAD_CONVERSATION_ID:
                return handle_bad_conversation_id(connection,
                                                  payload, payload_length,
                                                  error);
        case VSX_PROTO_CONVERSATION_FULL:
                return handle_conversation_full(connection,
                                                payload, payload_length,
                                                error);
        }

        return true;
}

static void
set_reconnect_timestamp(struct vsx_connection *connection)
{
        connection->reconnect_timestamp =
                vsx_monotonic_get() + connection->reconnect_timeout;

        if (connection->reconnect_timeout == 0) {
                connection->reconnect_timeout = VSX_CONNECTION_INITIAL_TIMEOUT;
        } else {
                /* Next time we need to try to reconnect we'll delay
                 * for twice as long, up to the maximum timeout
                 */
                connection->reconnect_timeout *= 2;
                if (connection->reconnect_timeout >
                    VSX_CONNECTION_MAX_TIMEOUT) {
                        connection->reconnect_timeout =
                                VSX_CONNECTION_MAX_TIMEOUT;
                }
        }
}

static void
report_error(struct vsx_connection *connection,
             struct vsx_error *error)
{
        close_socket(connection);

        /* If the connection managed to stay up for a while after
         * receiving the player_id command then we’ll assume it was
         * working and we can reconnect immediately without getting
         * into an infinite reconnect loop.
         */
        if (vsx_monotonic_get() - VSX_CONNECTION_STABLE_TIME >=
            connection->player_id_received_timestamp)
                connection->reconnect_timeout = 0;

        set_reconnect_timestamp(connection);
        update_poll(connection);
        vsx_connection_signal_error(connection, error);
}

static bool
get_payload_length(const uint8_t *buf,
                   size_t buf_length,
                   size_t *payload_length_out,
                   const uint8_t **payload_start_out)
{
        if (buf_length < 2)
                return false;

        switch (buf[1]) {
        case 0x7e: {
                if (buf_length < 4)
                        return false;

                uint16_t length;
                memcpy(&length, buf + 2, sizeof length);

                *payload_length_out = VSX_UINT16_FROM_BE(length);
                *payload_start_out = buf + 4;
                return true;
        }

        case 0x7f: {
                if (buf_length < 6)
                        return false;

                uint32_t length;
                memcpy(&length, buf + 2, sizeof length);

                *payload_length_out = VSX_UINT32_FROM_BE(length);
                *payload_start_out = buf + 6;
                return true;
        }

        default:
                *payload_length_out = buf[1];
                *payload_start_out = buf + 2;
                return true;
        }
}

static bool
is_would_block_error(int err)
{
        return err == EAGAIN || err == EWOULDBLOCK;
}

static const uint8_t *
find_ws_terminator(struct vsx_connection *connection)
{
        if (connection->ws_terminator_pos >= WS_TERMINATOR_LENGTH)
                return connection->input_buffer;

        const uint8_t *p = connection->input_buffer;

        while (p - connection->input_buffer < connection->input_length) {
                if (*(p++) != ws_terminator[connection->ws_terminator_pos]) {
                        connection->ws_terminator_pos = 0;
                        continue;
                }

                if (++connection->ws_terminator_pos >= WS_TERMINATOR_LENGTH)
                        return p;
        }

        /* If we make it here then we haven’t found the end of the
         * terminator yet.
         */
        return NULL;
}

static const uint8_t *
process_frames(struct vsx_connection *connection,
               const uint8_t *buf_start,
               size_t buf_length,
               struct vsx_error **error)
{
        const uint8_t *p = buf_start;
        size_t payload_length;
        const uint8_t *payload_start;

        while (get_payload_length(p,
                                  buf_start + buf_length - p,
                                  &payload_length,
                                  &payload_start)) {
                if (payload_length > VSX_PROTO_MAX_PAYLOAD_SIZE) {
                        vsx_set_error(error,
                                      &vsx_connection_error,
                                      VSX_CONNECTION_ERROR_BAD_DATA,
                                      "The server sent a frame that is too "
                                      "long");
                        return NULL;
                }

                if (payload_start + payload_length > buf_start + buf_length)
                        break;

                /* Ignore control frames and non-binary frames */
                if (*p == 0x82 &&
                    !process_message(connection,
                                     payload_start, payload_length,
                                     error))
                        return NULL;

                p = payload_start + payload_length;
        }

        return p;
}

static void
handle_read(struct vsx_connection *connection)
{
        ssize_t got = read(connection->sock,
                           connection->input_buffer + connection->input_length,
                           (sizeof connection->input_buffer) -
                           connection->input_length);
        if (got == -1) {
                if (!is_would_block_error(errno) && errno != EINTR) {
                        struct vsx_error *error = NULL;

                        vsx_file_error_set(&error,
                                           errno,
                                           "Error reading from socket: %s",
                                           strerror(errno));

                        report_error(connection, error);

                        vsx_error_free(error);
                }
        } else if (got == 0) {
                if (connection->finished) {
                        vsx_connection_set_running(connection, false);
                } else {
                        struct vsx_error *error = NULL;

                        vsx_set_error(&error,
                                      &vsx_connection_error,
                                      VSX_CONNECTION_ERROR_CONNECTION_CLOSED,
                                      "The server unexpectedly closed the "
                                      "connection");
                        report_error(connection, error);
                        vsx_error_free(error);
                }
        } else {
                connection->input_length += got;

                const uint8_t *p = find_ws_terminator (connection);

                if (p == NULL) {
                        /* Searching for the terminator consumed all
                         * of the input.
                         */
                        connection->input_length = 0;
                        return;
                }

                struct vsx_error *error = NULL;

                p = process_frames(connection,
                                   p,
                                   connection->input_buffer +
                                   connection->input_length -
                                   p,
                                   &error);

                if (p == NULL) {
                        report_error(connection, error);
                        vsx_error_free(error);
                        return;
                }

                memmove(connection->input_buffer,
                        p,
                        connection->input_buffer +
                        connection->input_length -
                        p);
                connection->input_length -= p - connection->input_buffer;

                update_poll(connection);
        }
}

static int
write_ws_request(struct vsx_connection *connection,
                 uint8_t *buffer, size_t buffer_size)
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

        memcpy(buffer, ws_request, ws_request_len);

        return ws_request_len;
}

static int
write_join_conversation_header(struct vsx_connection *connection,
                               uint8_t *buffer, size_t buffer_size)
{

        return vsx_proto_write_command(buffer,
                                       buffer_size,

                                       VSX_PROTO_JOIN_GAME,

                                       VSX_PROTO_TYPE_UINT64,
                                       connection->conversation_id,

                                       VSX_PROTO_TYPE_STRING,
                                       connection->player_name,

                                       VSX_PROTO_TYPE_NONE);
}

static int
write_join_public_game_header(struct vsx_connection *connection,
                              uint8_t *buffer, size_t buffer_size)
{

        return vsx_proto_write_command(buffer,
                                       buffer_size,

                                       VSX_PROTO_NEW_PLAYER,

                                       VSX_PROTO_TYPE_STRING,
                                       connection->room,

                                       VSX_PROTO_TYPE_STRING,
                                       connection->player_name,

                                       VSX_PROTO_TYPE_NONE);
}

static int
write_create_private_game_header(struct vsx_connection *connection,
                                 uint8_t *buffer, size_t buffer_size)
{

        return vsx_proto_write_command(buffer,
                                       buffer_size,

                                       VSX_PROTO_NEW_PRIVATE_GAME,

                                       VSX_PROTO_TYPE_STRING,
                                       connection->language_to_send,

                                       VSX_PROTO_TYPE_STRING,
                                       connection->player_name,

                                       VSX_PROTO_TYPE_NONE);
}

static int
write_header(struct vsx_connection *connection,
             uint8_t *buffer, size_t buffer_size)
{
        if (connection->has_person_id) {
                return vsx_proto_write_command(buffer,
                                               buffer_size,
                                               VSX_PROTO_RECONNECT,

                                               VSX_PROTO_TYPE_UINT64,
                                               connection->person_id,

                                               VSX_PROTO_TYPE_UINT16,
                                               connection->next_message_num,

                                               VSX_PROTO_TYPE_NONE);
        } else {
                /* If we don’t have a person ID then we need to create
                 * a new person. For that we need a player name. The
                 * connection shouldn’t have started if this isn’t set
                 * yet.
                 */
                assert(connection->player_name != NULL);

                if (connection->has_conversation_id) {
                        return write_join_conversation_header(connection,
                                                              buffer,
                                                              buffer_size);
                } else if (connection->room != NULL) {
                        return write_join_public_game_header(connection,
                                                             buffer,
                                                             buffer_size);
                } else {
                        return write_create_private_game_header(connection,
                                                                buffer,
                                                                buffer_size);
                }
        }
}

static int
write_keep_alive(struct vsx_connection *connection,
                 uint8_t *buffer, size_t buffer_size)
{
        return vsx_proto_write_command(buffer,
                                       buffer_size,

                                       VSX_PROTO_KEEP_ALIVE,

                                       VSX_PROTO_TYPE_NONE);
}

static int
write_n_tiles(struct vsx_connection *connection,
              uint8_t *buffer, size_t buffer_size)
{
        return vsx_proto_write_command(buffer,
                                       buffer_size,

                                       VSX_PROTO_SET_N_TILES,

                                       VSX_PROTO_TYPE_UINT8,
                                       connection->n_tiles_to_send,

                                       VSX_PROTO_TYPE_NONE);
}

static int
write_language(struct vsx_connection *connection,
               uint8_t *buffer, size_t buffer_size)
{
        return vsx_proto_write_command(buffer,
                                       buffer_size,

                                       VSX_PROTO_SET_LANGUAGE,

                                       VSX_PROTO_TYPE_STRING,
                                       connection->language_to_send,

                                       VSX_PROTO_TYPE_NONE);
}

static int
write_leave(struct vsx_connection *connection,
            uint8_t *buffer, size_t buffer_size)
{
        return vsx_proto_write_command(buffer,
                                       buffer_size,

                                       VSX_PROTO_LEAVE,

                                       VSX_PROTO_TYPE_NONE);
}

static int
write_shout(struct vsx_connection *connection,
            uint8_t *buffer, size_t buffer_size)
{
        return vsx_proto_write_command(buffer,
                                       buffer_size,

                                       VSX_PROTO_SHOUT,

                                       VSX_PROTO_TYPE_NONE);
}

static int
write_turn(struct vsx_connection *connection,
           uint8_t *buffer, size_t buffer_size)
{
        return vsx_proto_write_command(buffer,
                                       buffer_size,

                                       VSX_PROTO_TURN,

                                       VSX_PROTO_TYPE_NONE);
}

static int
write_move_tile(struct vsx_connection *connection,
                uint8_t *buffer, size_t buffer_size)
{
        if (vsx_list_empty(&connection->tiles_to_move))
                return 0;

        struct vsx_connection_tile_to_move *tile =
                vsx_container_of(connection->tiles_to_move.next,
                                 struct vsx_connection_tile_to_move,
                                 link);

        int ret = vsx_proto_write_command(buffer,
                                          buffer_size,

                                          VSX_PROTO_MOVE_TILE,

                                          VSX_PROTO_TYPE_UINT8,
                                          tile->num,

                                          VSX_PROTO_TYPE_INT16,
                                          tile->x,

                                          VSX_PROTO_TYPE_INT16,
                                          tile->y,

                                          VSX_PROTO_TYPE_NONE);

        if (ret > 0) {
                vsx_list_remove(&tile->link);
                vsx_free(tile);
        }

        return ret;
}

static int
write_send_message(struct vsx_connection *connection,
                   uint8_t *buffer, size_t buffer_size)
{
        if (vsx_list_empty(&connection->messages_to_send))
                return 0;

        struct vsx_connection_message_to_send *message =
                vsx_container_of(connection->messages_to_send.next,
                                 struct vsx_connection_message_to_send,
                                 link);

        int ret = vsx_proto_write_command(buffer,
                                          buffer_size,

                                          VSX_PROTO_SEND_MESSAGE,

                                          VSX_PROTO_TYPE_STRING,
                                          message->message,

                                          VSX_PROTO_TYPE_NONE);

        if (ret > 0) {
                /* The server automatically assumes we're not typing
                 * anymore when the client sends a message
                 */
                connection->sent_typing_state = false;

                vsx_list_remove(&message->link);
                vsx_free(message);
        }

        return ret;
}

static int
write_typing_state(struct vsx_connection *connection,
                   uint8_t *buffer, size_t buffer_size)
{
        if (connection->typing == connection->sent_typing_state)
                return 0;

        int ret = vsx_proto_write_command(buffer,
                                          buffer_size,

                                          connection->typing ?
                                          VSX_PROTO_START_TYPING :
                                          VSX_PROTO_STOP_TYPING,

                                          VSX_PROTO_TYPE_NONE);

        if (ret > 0)
                connection->sent_typing_state = connection->typing;

        return ret;
}

static int
write_one_item(struct vsx_connection *connection)
{
        static const struct {
                enum vsx_connection_dirty_flag flag;
                vsx_connection_write_state_func func;
        } write_funcs[] = {
                { VSX_CONNECTION_DIRTY_FLAG_WS_HEADER, write_ws_request },
                { VSX_CONNECTION_DIRTY_FLAG_HEADER, write_header },
                { VSX_CONNECTION_DIRTY_FLAG_KEEP_ALIVE, write_keep_alive },
                { VSX_CONNECTION_DIRTY_FLAG_N_TILES, write_n_tiles },
                { VSX_CONNECTION_DIRTY_FLAG_LANGUAGE, write_language },
                { VSX_CONNECTION_DIRTY_FLAG_LEAVE, write_leave },
                { VSX_CONNECTION_DIRTY_FLAG_SHOUT, write_shout },
                { VSX_CONNECTION_DIRTY_FLAG_TURN, write_turn },
                { .func = write_move_tile },
                { .func = write_send_message },
                { .func = write_typing_state },
        };

        for (int i = 0; i < VSX_N_ELEMENTS(write_funcs); i++) {
                if (write_funcs[i].flag != 0 &&
                    (connection->dirty_flags & write_funcs[i].flag) == 0)
                        continue;

                size_t space_left =
                        (sizeof connection->output_buffer) -
                        connection->output_length;

                int wrote = write_funcs[i].func(connection,
                                                connection->output_buffer +
                                                connection->output_length,
                                                space_left);

                if (wrote == -1)
                        return -1;

                connection->dirty_flags &= ~write_funcs[i].flag;

                if (wrote == 0)
                        continue;

                connection->output_length += wrote;

                return wrote;
        }

        return 0;
}

static void
fill_output_buffer(struct vsx_connection *connection)
{
        int wrote;

        do {
                wrote = write_one_item(connection);
        } while (wrote > 0);
}

static void
handle_write(struct vsx_connection *connection)
{
        fill_output_buffer(connection);

        size_t wrote = write(connection->sock,
                             connection->output_buffer,
                             connection->output_length);

        if (wrote == -1) {
                if (!is_would_block_error(errno) && errno != EINTR) {
                        struct vsx_error *error = NULL;

                        vsx_file_error_set(&error,
                                           errno,
                                           "Error writing to socket: %s",
                                           strerror(errno));

                        report_error(connection, error);

                        vsx_error_free(error);
                }
        } else {
                /* Move any remaining data in the output buffer to the front */
                memmove(connection->output_buffer,
                        connection->output_buffer + wrote,
                        connection->output_length - wrote);
                connection->output_length -= wrote;

                connection->keep_alive_timestamp =
                        vsx_monotonic_get() + VSX_CONNECTION_KEEP_ALIVE_TIME;

                update_poll(connection);
        }
}

static void
try_reconnect(struct vsx_connection *connection)
{
        connection->reconnect_timestamp = INT64_MAX;
        connection->player_id_received_timestamp = INT64_MAX;

        close_socket(connection);

        struct vsx_error *error = NULL;

        struct vsx_netaddress_native address;

        vsx_netaddress_to_native(&connection->address, &address);

        connection->sock = socket(address.sockaddr.sa_family == AF_INET6 ?
                                  PF_INET6 :
                                  PF_INET,
                                  SOCK_STREAM,
                                  0);

        if (connection->sock == -1)
                goto file_error;

        if (!vsx_socket_set_nonblock(connection->sock, &error))
                goto error;

        int connect_ret = connect(connection->sock,
                                  &address.sockaddr,
                                  address.length);

        if (connect_ret == 0) {
                connection->running_state =
                        VSX_CONNECTION_RUNNING_STATE_RUNNING;
        } else if (errno == EINPROGRESS) {
                connection->running_state =
                        VSX_CONNECTION_RUNNING_STATE_RECONNECTING;
        } else {
                goto file_error;
        }

        connection->dirty_flags |= (VSX_CONNECTION_DIRTY_FLAG_WS_HEADER |
                                    VSX_CONNECTION_DIRTY_FLAG_HEADER);
        connection->output_length = 0;
        connection->input_length = 0;
        connection->ws_terminator_pos = 0;
        connection->write_finished = false;
        connection->synced = false;

        update_poll(connection);

        return;

file_error:
        vsx_file_error_set(&error,
                           errno,
                           "Error connecting: %s",
                           strerror(errno));
error:
        report_error(connection, error);

        vsx_error_free(error);
}

void
vsx_connection_wake_up(struct vsx_connection *connection,
                       short poll_events)
{
        int64_t now = vsx_monotonic_get();

        if (connection->sock == -1) {
                if (now >= connection->reconnect_timestamp)
                        try_reconnect(connection);
                return;
        }

        if (connection->running_state ==
            VSX_CONNECTION_RUNNING_STATE_RECONNECTING &&
            (poll_events & POLLOUT)) {
                connection->running_state =
                        VSX_CONNECTION_RUNNING_STATE_RUNNING;
        }

        if (now >= connection->keep_alive_timestamp) {
                connection->keep_alive_timestamp = INT64_MAX;
                connection->dirty_flags |= VSX_CONNECTION_DIRTY_FLAG_KEEP_ALIVE;
        }

        if ((poll_events & (POLLIN | POLLERR | POLLHUP)))
                handle_read(connection);
        else if ((poll_events & POLLOUT))
                handle_write(connection);
        else
                update_poll(connection);
}

static bool
has_pending_data(struct vsx_connection *connection)
{
        if (connection->output_length > 0)
                return true;

        if (connection->dirty_flags)
                return true;

        if (!vsx_list_empty(&connection->tiles_to_move))
                return true;

        if (!vsx_list_empty(&connection->messages_to_send))
                return true;

        if (connection->sent_typing_state != connection->typing)
                return true;

        return false;
}

static short
calculate_poll_events(struct vsx_connection *connection)
{
        short events = 0;

        switch (connection->running_state) {
        case VSX_CONNECTION_RUNNING_STATE_RECONNECTING:
                events = POLLOUT;
                break;

        case VSX_CONNECTION_RUNNING_STATE_RUNNING:
                events = POLLIN;

                if (!connection->write_finished) {
                        if (has_pending_data(connection)) {
                                events |= POLLOUT;
                        } else if (connection->finished) {
                                shutdown(connection->sock, SHUT_WR);

                                connection->write_finished = true;
                        }
                }
                break;

        default:
                break;
        }

        return events;
}

static int64_t
calculate_wakeup_timestamp(struct vsx_connection *connection)
{
        int64_t wakeup_timestamp = INT64_MAX;

        if (connection->reconnect_timestamp < wakeup_timestamp)
                wakeup_timestamp = connection->reconnect_timestamp;

        if (connection->keep_alive_timestamp < wakeup_timestamp)
                wakeup_timestamp = connection->keep_alive_timestamp;

        return wakeup_timestamp;
}

static void
update_poll(struct vsx_connection *connection)
{
        short events = calculate_poll_events(connection);
        int64_t wakeup_timestamp = calculate_wakeup_timestamp(connection);

        if (connection->poll_changed_event.poll_changed.fd !=
            connection->sock ||
            connection->poll_changed_event.poll_changed.events != events ||
            connection->poll_changed_event.poll_changed.wakeup_time !=
            wakeup_timestamp) {
                connection->poll_changed_event.poll_changed.fd =
                        connection->sock;
                connection->poll_changed_event.poll_changed.events =
                        events;
                connection->poll_changed_event.poll_changed.wakeup_time =
                        wakeup_timestamp;

                emit_event(connection, &connection->poll_changed_event);
        }
}

static bool
has_configuration(struct vsx_connection *connection)
{
        /* We always need a server address to connect to */
        if (!connection->has_address)
                return false;

        /* If we have a person ID that we don’t need any of the other
         * information.
         */
        if (connection->has_person_id)
                return true;

        /* In order to make a person we always need the name */
        if (connection->player_name == NULL)
                return false;

        return true;
}

static void
start_connecting_running_state(struct vsx_connection *connection)
{
        /* Reset the retry timeout because this is a first attempt at
         * connecting
         */
        connection->reconnect_timeout = 0;

        if (has_configuration(connection)) {
                connection->reconnect_timestamp = vsx_monotonic_get();

                connection->running_state =
                        VSX_CONNECTION_RUNNING_STATE_WAITING_FOR_RECONNECT;

                update_poll(connection);
        } else {
                connection->reconnect_timestamp = INT64_MAX;
                connection->running_state =
                        VSX_CONNECTION_RUNNING_STATE_WAITING_FOR_CONFIGURATION;
        }
}

static void
vsx_connection_set_running_internal(struct vsx_connection *connection,
                                    bool running)
{
        if (running) {
                if (connection->running_state ==
                    VSX_CONNECTION_RUNNING_STATE_DISCONNECTED) {
                        start_connecting_running_state (connection);
                }
        } else {
                switch (connection->running_state) {
                case VSX_CONNECTION_RUNNING_STATE_DISCONNECTED:
                        /* already disconnected */
                        break;

                case VSX_CONNECTION_RUNNING_STATE_RECONNECTING:
                case VSX_CONNECTION_RUNNING_STATE_RUNNING:
                        connection->running_state =
                                VSX_CONNECTION_RUNNING_STATE_DISCONNECTED;
                        close_socket(connection);
                        update_poll(connection);
                        break;

                case VSX_CONNECTION_RUNNING_STATE_WAITING_FOR_RECONNECT:
                        /* Cancel the timeout */
                        connection->reconnect_timestamp = INT64_MAX;
                        connection->running_state =
                                VSX_CONNECTION_RUNNING_STATE_DISCONNECTED;
                        update_poll(connection);
                        break;

                case VSX_CONNECTION_RUNNING_STATE_WAITING_FOR_CONFIGURATION:
                        connection->running_state =
                                VSX_CONNECTION_RUNNING_STATE_DISCONNECTED;
                        break;
                }
        }
}

void
vsx_connection_set_running(struct vsx_connection *connection,
                           bool running)
{
        if (running == vsx_connection_get_running(connection))
                return;

        vsx_connection_set_running_internal(connection, running);

        struct vsx_connection_event event = {
                .type = VSX_CONNECTION_EVENT_TYPE_RUNNING_STATE_CHANGED,
                .running_state_changed = { .running = running },
        };

        emit_event(connection, &event);
}

bool
vsx_connection_get_running(struct vsx_connection *connection)
{
        return (connection->running_state !=
                VSX_CONNECTION_RUNNING_STATE_DISCONNECTED);
}

static void
free_tiles_to_move(struct vsx_connection *connection)
{
        struct vsx_connection_tile_to_move *tile, *tmp;

        vsx_list_for_each_safe(tile, tmp, &connection->tiles_to_move, link) {
                vsx_free(tile);
        }

        vsx_list_init(&connection->tiles_to_move);
}

static void
free_messages_to_send(struct vsx_connection *connection)
{
        struct vsx_connection_message_to_send *message, *tmp;

        vsx_list_for_each_safe(message,
                               tmp,
                               &connection->messages_to_send,
                               link) {
                vsx_free(message);
        }

        vsx_list_init(&connection->messages_to_send);
}

void
vsx_connection_reset(struct vsx_connection *connection)
{
        close_socket(connection);

        vsx_free(connection->room);
        connection->room = NULL;

        vsx_free(connection->player_name);
        connection->player_name = NULL;

        connection->has_person_id = false;
        connection->has_conversation_id = false;
        connection->finished = false;
        connection->typing = false;
        connection->sent_typing_state = false;
        connection->next_message_num = 0;
        connection->language_to_send[0] = '\0';

        connection->dirty_flags = 0;

        connection->keep_alive_timestamp = INT64_MAX;
        connection->reconnect_timestamp = INT64_MAX;
        connection->player_id_received_timestamp = INT64_MAX;

        connection->reconnect_timeout = 0;

        free_tiles_to_move(connection);
        free_messages_to_send(connection);

        vsx_connection_set_running(connection, false);

        update_poll(connection);
}

struct vsx_connection *
vsx_connection_new(void)
{
        struct vsx_connection *connection = vsx_calloc(sizeof *connection);

        vsx_signal_init(&connection->event_signal);

        connection->poll_changed_event.type =
                VSX_CONNECTION_EVENT_TYPE_POLL_CHANGED;

        connection->sock = -1;

        vsx_list_init(&connection->tiles_to_move);
        vsx_list_init(&connection->messages_to_send);

        vsx_connection_reset(connection);

        return connection;
}

void
vsx_connection_free(struct vsx_connection *connection)
{
        /* Reinitialise the signal so that reset the connection won’t
         * emit the poll changed event.
         */
        vsx_signal_init(&connection->event_signal);

        vsx_connection_reset(connection);

        vsx_free(connection);
}

bool
vsx_connection_get_typing(struct vsx_connection *connection)
{
        return connection->typing;
}

void
vsx_connection_send_message(struct vsx_connection *connection,
                            const char *message)
{
        size_t message_length = strlen(message);

        if (message_length > VSX_PROTO_MAX_MESSAGE_LENGTH) {
                message_length = VSX_PROTO_MAX_MESSAGE_LENGTH;
                /* If we’ve clipped before a continuation byte then
                 * also clip the rest of the UTF-8 sequence so that it
                 * will remain valid UTF-8.
                 */
                while ((message[message_length] & 0xc0) == 0x80)
                        message_length--;
        }

        struct vsx_connection_message_to_send *message_to_send =
                vsx_alloc(offsetof(struct vsx_connection_message_to_send,
                                   message) +
                          message_length +
                          1);

        memcpy(message_to_send->message, message, message_length);
        message_to_send->message[message_length] = '\0';

        vsx_list_insert(connection->messages_to_send.prev,
                        &message_to_send->link);

        update_poll(connection);
}

void
vsx_connection_leave(struct vsx_connection *connection)
{
        connection->dirty_flags |= VSX_CONNECTION_DIRTY_FLAG_LEAVE;

        update_poll(connection);
}

struct vsx_signal *
vsx_connection_get_event_signal(struct vsx_connection *connection)
{
        return &connection->event_signal;
}

bool
vsx_connection_get_person_id(struct vsx_connection *connection,
                             uint64_t *person_id)
{
        if (connection->has_person_id) {
                *person_id = connection->person_id;
                return true;
        }

        return false;
}

static void
maybe_start_connecting_running_state(struct vsx_connection *connection)
{
        if (connection->running_state ==
            VSX_CONNECTION_RUNNING_STATE_WAITING_FOR_CONFIGURATION)
                start_connecting_running_state(connection);
}

void
vsx_connection_set_person_id(struct vsx_connection *connection,
                             uint64_t person_id)
{
        if (connection->has_person_id)
                return;

        connection->person_id = person_id;
        connection->has_person_id = true;

        maybe_start_connecting_running_state(connection);
}

void
vsx_connection_set_player_name(struct vsx_connection *connection,
                               const char *player_name)
{
        if (connection->player_name)
                return;

        connection->player_name = vsx_strdup(player_name);

        maybe_start_connecting_running_state(connection);
}

void
vsx_connection_set_room(struct vsx_connection *connection,
                        const char *room)
{
        if (connection->room)
                return;

        connection->room = vsx_strdup(room);

        maybe_start_connecting_running_state(connection);
}

void
vsx_connection_set_conversation_id(struct vsx_connection *connection,
                                   uint64_t conversation_id)
{
        if (connection->has_conversation_id)
                return;

        connection->has_conversation_id = true;
        connection->conversation_id = conversation_id;

        maybe_start_connecting_running_state(connection);
}

void
vsx_connection_set_address(struct vsx_connection *connection,
                           const struct vsx_netaddress *address)
{
        if (connection->has_address)
                return;

        connection->has_address = true;
        connection->address = *address;

        maybe_start_connecting_running_state(connection);
}

void
vsx_connection_copy_event(struct vsx_connection_event *dest,
                          const struct vsx_connection_event *src)
{
        *dest = *src;

        switch (src->type) {
        case VSX_CONNECTION_EVENT_TYPE_ERROR:
                dest->error.error = NULL;
                vsx_set_error(&dest->error.error,
                              src->error.error->domain,
                              src->error.error->code,
                              "%s",
                              src->error.error->message);
                break;
        case VSX_CONNECTION_EVENT_TYPE_MESSAGE:
                dest->message.message = vsx_strdup(src->message.message);
                break;
        case VSX_CONNECTION_EVENT_TYPE_PLAYER_NAME_CHANGED:
                dest->player_name_changed.name =
                        vsx_strdup(src->player_name_changed.name);
                break;
        case VSX_CONNECTION_EVENT_TYPE_LANGUAGE_CHANGED:
                dest->language_changed.code =
                        vsx_strdup(src->language_changed.code);
                break;
        case VSX_CONNECTION_EVENT_TYPE_HEADER:
        case VSX_CONNECTION_EVENT_TYPE_CONVERSATION_ID:
        case VSX_CONNECTION_EVENT_TYPE_PLAYER_FLAGS_CHANGED:
        case VSX_CONNECTION_EVENT_TYPE_PLAYER_SHOUTED:
        case VSX_CONNECTION_EVENT_TYPE_TILE_CHANGED:
        case VSX_CONNECTION_EVENT_TYPE_N_TILES_CHANGED:
        case VSX_CONNECTION_EVENT_TYPE_RUNNING_STATE_CHANGED:
        case VSX_CONNECTION_EVENT_TYPE_END:
        case VSX_CONNECTION_EVENT_TYPE_POLL_CHANGED:
                break;
        }
}

void
vsx_connection_destroy_event(struct vsx_connection_event *event)
{
        switch (event->type) {
        case VSX_CONNECTION_EVENT_TYPE_ERROR:
                vsx_error_free(event->error.error);
                break;
        case VSX_CONNECTION_EVENT_TYPE_MESSAGE:
                vsx_free((char *) event->message.message);
                break;
        case VSX_CONNECTION_EVENT_TYPE_PLAYER_NAME_CHANGED:
                vsx_free((char *) event->player_name_changed.name);
                break;
        case VSX_CONNECTION_EVENT_TYPE_LANGUAGE_CHANGED:
                vsx_free((char *) event->language_changed.code);
                break;
        case VSX_CONNECTION_EVENT_TYPE_HEADER:
        case VSX_CONNECTION_EVENT_TYPE_CONVERSATION_ID:
        case VSX_CONNECTION_EVENT_TYPE_PLAYER_FLAGS_CHANGED:
        case VSX_CONNECTION_EVENT_TYPE_PLAYER_SHOUTED:
        case VSX_CONNECTION_EVENT_TYPE_TILE_CHANGED:
        case VSX_CONNECTION_EVENT_TYPE_N_TILES_CHANGED:
        case VSX_CONNECTION_EVENT_TYPE_RUNNING_STATE_CHANGED:
        case VSX_CONNECTION_EVENT_TYPE_END:
        case VSX_CONNECTION_EVENT_TYPE_POLL_CHANGED:
                break;
        }
}
