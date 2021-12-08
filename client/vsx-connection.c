/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2012, 2013, 2021  Neil Roberts
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
};

typedef int
(* vsx_connection_write_state_func)(struct vsx_connection *conn,
                                    uint8_t *buffer,
                                    size_t buffer_size);

struct vsx_error_domain
vsx_connection_error;

static void
update_poll(struct vsx_connection *connection,
            bool always_send);

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

enum vsx_connection_running_state {
        VSX_CONNECTION_RUNNING_STATE_DISCONNECTED,
        /* connect has been called and we are waiting for it to
         * become ready for writing
         */
        VSX_CONNECTION_RUNNING_STATE_RECONNECTING,
        VSX_CONNECTION_RUNNING_STATE_RUNNING,
        VSX_CONNECTION_RUNNING_STATE_WAITING_FOR_RECONNECT
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
        struct vsx_netaddress address;
        char *room;
        char *player_name;
        struct vsx_player *self;
        bool has_person_id;
        uint64_t person_id;
        enum vsx_connection_running_state running_state;
        enum vsx_connection_state state;
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

        struct vsx_signal event_signal;

        enum vsx_connection_dirty_flag dirty_flags;
        struct vsx_list tiles_to_move;
        struct vsx_list messages_to_send;

        int sock;
        /* The condition that the source was last created with so we can
         * know if we need to recreate it.
         */
        short sock_events;

        /* Array of pointers to players, indexed by player num. This can
         * have NULL gaps.
         */
        struct vsx_buffer players;

        /* Slab allocator for vsx_tile */
        struct vsx_slab_allocator tile_allocator;
        /* Array of pointers to tiles, indexed by tile num. This can have
         * NULL gaps.
         */
        struct vsx_buffer tiles;

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
send_poll_changed(struct vsx_connection *connection)
{
        int64_t wakeup_time = INT64_MAX;

        if (connection->reconnect_timestamp < wakeup_time)
                wakeup_time = connection->reconnect_timestamp;

        if (connection->keep_alive_timestamp < wakeup_time)
                wakeup_time = connection->keep_alive_timestamp;

        struct vsx_connection_event event = {
                .type = VSX_CONNECTION_EVENT_TYPE_POLL_CHANGED,
                .poll_changed = {
                        .wakeup_time = wakeup_time,
                        .fd = connection->sock,
                        .events = connection->sock_events,
                },
        };

        vsx_signal_emit(&connection->event_signal, &event);
}

static void
vsx_connection_signal_error(struct vsx_connection *connection,
                            struct vsx_error *error)
{
        struct vsx_connection_event event = {
                .type = VSX_CONNECTION_EVENT_TYPE_ERROR,
                .error = { .error = error },
        };

        vsx_signal_emit(&connection->event_signal, &event);
}

static void
close_socket(struct vsx_connection *connection)
{
        if (connection->sock != -1) {
                vsx_close(connection->sock);
                connection->sock = -1;
        }
}

void
vsx_connection_set_typing(struct vsx_connection *connection,
                          bool typing)
{
        if (connection->typing != typing) {
                connection->typing = typing;
                update_poll(connection, false /* always_send */);
        }
}

void
vsx_connection_shout(struct vsx_connection *connection)
{
        connection->dirty_flags |= VSX_CONNECTION_DIRTY_FLAG_SHOUT;

        update_poll(connection, false /* always_send */);
}

void
vsx_connection_turn(struct vsx_connection *connection)
{
        connection->dirty_flags |= VSX_CONNECTION_DIRTY_FLAG_TURN;

        update_poll(connection, false /* always_send */);
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

        update_poll(connection, false /* always_send */);
}

static void
vsx_connection_set_state(struct vsx_connection *connection,
                         enum vsx_connection_state state)
{
        if (connection->state == state)
                return;

        connection->state = state;

        struct vsx_connection_event event = {
                .type = VSX_CONNECTION_EVENT_TYPE_STATE_CHANGED,
                .state_changed = { .state = state },
        };

        vsx_signal_emit(&connection->event_signal, &event);
}

static void *
get_pointer_from_buffer(struct vsx_buffer *buf, int num)
{
        size_t n_entries = buf->length / sizeof (void *);

        if (num >= n_entries)
                return NULL;

        return ((void **) buf->data)[num];
}

static void
set_pointer_in_buffer(struct vsx_buffer *buf,
                      int num,
                      void *value)
{
        size_t n_entries = buf->length / sizeof (void *);

        if (num >= n_entries) {
                size_t old_length = buf->length;
                vsx_buffer_set_length(buf, (num + 1) * sizeof (void *));
                memset(buf->data + old_length,
                       0,
                       num * sizeof (void *) - old_length);
        }

        ((void **) buf->data)[num] = value;
}

static struct vsx_player *
get_or_create_player(struct vsx_connection *connection,
                     int player_num)
{
        struct vsx_player *player =
                get_pointer_from_buffer(&connection->players, player_num);

        if (player == NULL) {
                player = vsx_calloc(sizeof *player);
                player->num = player_num;
                set_pointer_in_buffer(&connection->players, player_num, player);
        }

        return player;
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

        connection->self = get_or_create_player(connection, self_num);

        connection->has_person_id = true;

        if (connection->state == VSX_CONNECTION_STATE_AWAITING_HEADER) {
                vsx_connection_set_state(connection,
                                         VSX_CONNECTION_STATE_IN_PROGRESS);
        }

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
                        .player = get_or_create_player(connection, person),
                        .message = text,
                },
        };

        vsx_signal_emit(&connection->event_signal, &event);

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

        bool is_new = false;

        struct vsx_tile *tile =
                get_pointer_from_buffer(&connection->tiles, num);

        if (tile == NULL) {
                tile = vsx_slab_allocate(&connection->tile_allocator,
                                         sizeof *tile,
                                         alignof *tile);

                tile->num = num;

                set_pointer_in_buffer(&connection->tiles, num, tile);

                is_new = true;
        }

        tile->x = x;
        tile->y = y;
        tile->letter = vsx_utf8_get_char(letter);

        struct vsx_connection_event event = {
                .type = VSX_CONNECTION_EVENT_TYPE_TILE_CHANGED,
                .tile_changed = {
                        .new_tile = is_new,
                        .tile = tile,
                },
        };

        vsx_signal_emit(&connection->event_signal, &event);

        return true;
}

static void
emit_player_changed(struct vsx_connection *connection,
                    struct vsx_player *player)
{
        struct vsx_connection_event event = {
                .type = VSX_CONNECTION_EVENT_TYPE_PLAYER_CHANGED,
                .player_changed = { .player = player },
        };

        vsx_signal_emit(&connection->event_signal, &event);
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

        struct vsx_player *player = get_or_create_player(connection, num);

        vsx_free(player->name);
        player->name = vsx_strdup(name);

        emit_player_changed(connection, player);

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

        struct vsx_player *player = get_or_create_player(connection, num);

        player->flags = flags;

        emit_player_changed(connection, player);

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
                        .player = get_or_create_player(connection, player_num)
                },
        };

        vsx_signal_emit(&connection->event_signal, &event);

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

        vsx_connection_set_state(connection, VSX_CONNECTION_STATE_DONE);

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
        case VSX_PROTO_END:
                return handle_end(connection,
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
        set_reconnect_timestamp(connection);
        send_poll_changed(connection);
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
        case 0x7e:
                if (buf_length < 4)
                        return false;

                *payload_length_out = vsx_proto_read_uint16_t(buf + 2);
                *payload_start_out = buf + 4;
                return true;

        case 0x7f:
                if (buf_length < 6)
                        return false;

                *payload_length_out = vsx_proto_read_uint32_t(buf + 2);
                *payload_start_out = buf + 6;
                return true;

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
                                      VSX_CONNECTION_ERROR_CONNECTION_CLOSED,
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
                if (connection->state == VSX_CONNECTION_STATE_DONE) {
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

                update_poll(connection, false /* always_send */ );
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
                return vsx_proto_write_command(buffer,
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
write_keep_alive(struct vsx_connection *connection,
                 uint8_t *buffer, size_t buffer_size)
{
        return vsx_proto_write_command(buffer,
                                       buffer_size,

                                       VSX_PROTO_KEEP_ALIVE,

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

                update_poll(connection, true /* always_send */ );
        }
}

static void
try_reconnect(struct vsx_connection *connection)
{
        connection->reconnect_timestamp = INT64_MAX;

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

        short events;

        if (connect_ret == 0) {
                connection->running_state =
                        VSX_CONNECTION_RUNNING_STATE_RUNNING;
                events = POLLIN | POLLOUT;
        } else if (errno == EINPROGRESS) {
                connection->running_state =
                        VSX_CONNECTION_RUNNING_STATE_RECONNECTING;
                events = POLLOUT;
        } else {
                goto file_error;
        }

        connection->dirty_flags |= (VSX_CONNECTION_DIRTY_FLAG_WS_HEADER |
                                    VSX_CONNECTION_DIRTY_FLAG_HEADER);
        connection->ws_terminator_pos = 0;
        connection->write_finished = false;

        connection->sock_events = events;
        send_poll_changed(connection);

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
                update_poll(connection, false /* always_send */ );
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

static void
update_poll(struct vsx_connection *connection, bool always_send)
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
                        } else if (connection->self &&
                                   !vsx_player_is_connected(connection->self)) {
                                shutdown(connection->sock, SHUT_WR);

                                connection->write_finished = true;
                        }
                }
                break;

        default:
                return;
        }

        if (always_send || connection->sock_events != events) {
                connection->sock_events = events;

                send_poll_changed(connection);
        }
}

static void
start_connecting_running_state(struct vsx_connection *connection)
{
        /* Reset the retry timeout because this is a first attempt at
         * connecting
         */
        connection->reconnect_timeout = 0;

        connection->running_state =
                VSX_CONNECTION_RUNNING_STATE_WAITING_FOR_RECONNECT;

        set_reconnect_timestamp(connection);
        send_poll_changed(connection);
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
                        send_poll_changed(connection);
                        break;

                case VSX_CONNECTION_RUNNING_STATE_WAITING_FOR_RECONNECT:
                        /* Cancel the timeout */
                        connection->reconnect_timestamp = INT64_MAX;
                        connection->running_state =
                                VSX_CONNECTION_RUNNING_STATE_DISCONNECTED;
                        send_poll_changed(connection);
                        break;
                }
        }
}

void
vsx_connection_set_running(struct vsx_connection *connection,
                           bool running)
{
        vsx_connection_set_running_internal(connection, running);

        struct vsx_connection_event event = {
                .type = VSX_CONNECTION_EVENT_TYPE_RUNNING_STATE_CHANGED,
                .running_state_changed = { .running = running },
        };

        vsx_signal_emit(&connection->event_signal, &event);
}

bool
vsx_connection_get_running(struct vsx_connection *connection)
{
        return (connection->running_state !=
                VSX_CONNECTION_RUNNING_STATE_DISCONNECTED);
}

struct vsx_connection *
vsx_connection_new(const struct vsx_netaddress *address,
                   const char *room,
                   const char *player_name)
{
        struct vsx_connection *connection = vsx_calloc(sizeof *connection);

        if (address == NULL) {
                vsx_netaddress_from_string(&connection->address,
                                           "127.0.0.1",
                                           5144);
        } else {
                connection->address = *address;
        }

        vsx_signal_init(&connection->event_signal);

        connection->keep_alive_timestamp = INT64_MAX;
        connection->reconnect_timestamp = INT64_MAX;

        connection->sock = -1;

        connection->room = vsx_strdup(room);
        connection->player_name = vsx_strdup(player_name);

        connection->next_message_num = 0;

        vsx_buffer_init(&connection->players);

        vsx_slab_init(&connection->tile_allocator);
        vsx_buffer_init(&connection->tiles);

        vsx_list_init(&connection->tiles_to_move);
        vsx_list_init(&connection->messages_to_send);

        return connection;
}

static void
free_tiles_to_move(struct vsx_connection *connection)
{
        struct vsx_connection_tile_to_move *tile, *tmp;

        vsx_list_for_each_safe(tile, tmp, &connection->tiles_to_move, link) {
                vsx_free(tile);
        }
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
}

static void
free_players(struct vsx_connection *connection)
{
        int n_players =
                connection->players.length / sizeof (struct vsx_player *);

        for (int i = 0; i < n_players; i++) {
                struct vsx_player *player =
                        ((struct vsx_player **) connection->players.data)[i];

                if (player == NULL)
                        continue;

                vsx_free(player->name);
                vsx_free(player);
        }

        vsx_buffer_destroy(&connection->players);
}

void
vsx_connection_free(struct vsx_connection *connection)
{
        close_socket(connection);

        vsx_free(connection->room);
        vsx_free(connection->player_name);

        free_players(connection);

        vsx_buffer_destroy(&connection->tiles);
        vsx_slab_destroy(&connection->tile_allocator);

        free_tiles_to_move(connection);
        free_messages_to_send(connection);

        vsx_free(connection);
}

bool
vsx_connection_get_typing(struct vsx_connection *connection)
{
        return connection->typing;
}

enum vsx_connection_state
vsx_connection_get_state(struct vsx_connection *connection)
{
        return connection->state;
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

        update_poll(connection, false /* always_send */);
}

void
vsx_connection_leave(struct vsx_connection *connection)
{
        connection->dirty_flags |= VSX_CONNECTION_DIRTY_FLAG_LEAVE;

        update_poll(connection, false /* always_send */);
}

const struct vsx_player *
vsx_connection_get_player(struct vsx_connection *connection, int player_num)
{
        return get_pointer_from_buffer(&connection->players, player_num);
}

void
vsx_connection_foreach_player(struct vsx_connection *connection,
                              vsx_connection_foreach_player_cb callback,
                              void *user_data)
{
        int n_players =
                connection->players.length / sizeof (struct vsx_player *);

        for (int i = 0; i < n_players; i++) {
                struct vsx_player *player =
                        ((struct vsx_player **)connection->players.data)[i];

                if (player == NULL)
                        continue;

                callback(player, user_data);
        }
}

const struct vsx_player *
vsx_connection_get_self(struct vsx_connection *connection)
{
        return connection->self;
}

const struct vsx_tile *
vsx_connection_get_tile(struct vsx_connection *connection,
                        int tile_num)
{
        return get_pointer_from_buffer(&connection->tiles, tile_num);
}

void
vsx_connection_foreach_tile(struct vsx_connection *connection,
                            vsx_connection_foreach_tile_cb callback,
                            void *user_data)
{
        int n_tiles = connection->tiles.length / sizeof (struct vsx_tile *);

        for (int i = 0; i < n_tiles; i++) {
                struct vsx_tile *tile =
                        ((struct vsx_tile **)connection->tiles.data)[i];

                if (tile == NULL)
                        continue;

                callback(tile, user_data);
        }
}

struct vsx_signal *
vsx_connection_get_event_signal(struct vsx_connection *connection)
{
        return &connection->event_signal;
}
