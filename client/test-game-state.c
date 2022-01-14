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
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <inttypes.h>
#include <math.h>

#include "vsx-connection.h"
#include "vsx-worker.h"
#include "vsx-util.h"
#include "vsx-proto.h"
#include "vsx-game-state.h"
#include "vsx-main-thread.h"
#include "vsx-bitmask.h"
#include "vsx-monotonic.h"

#define TEST_PORT 6138

struct harness {
        int server_sock;
        struct vsx_connection *connection;
        struct vsx_worker *worker;
        struct vsx_game_state *game_state;

        int server_fd;

        bool idle_queued;
};

typedef bool
(* check_event_func)(struct harness *harness,
                     const struct vsx_connection_event *event,
                     void *user_data);

typedef bool
(* check_modified_func)(struct harness *harness,
                        const struct vsx_game_state_modified_event *event,
                        void *user_data);

struct check_event_setup {
        enum vsx_connection_event_type expected_event_type;
        check_event_func event_cb;
        enum vsx_game_state_modified_type expected_modified_type;
        check_modified_func modified_cb;
};

struct check_event_listener {
        struct check_event_setup setup;
        struct vsx_listener event_listener;
        struct vsx_listener modified_listener;

        bool succeeded;

        struct harness *harness;
        void *user_data;
};

static void
free_harness(struct harness *harness)
{
        if (harness->game_state)
                vsx_game_state_free(harness->game_state);

        if (harness->worker)
                vsx_worker_free(harness->worker);

        if (harness->connection)
                vsx_connection_free(harness->connection);

        if (harness->server_fd != -1)
                vsx_close(harness->server_fd);

        if (harness->server_sock != -1)
                vsx_close(harness->server_sock);

        vsx_free(harness);
}

static bool
write_data(struct harness *harness,
           const uint8_t *data,
           size_t data_len)
{
        ssize_t wrote = write(harness->server_fd, data, data_len);

        if (wrote == -1) {
                fprintf(stderr,
                        "Error writing to server_fd: %s\n",
                        strerror(errno));
                return false;
        }

        if (wrote != data_len) {
                fprintf(stderr,
                        "Tried to write %zu bytes but write returned %i\n",
                        data_len,
                        (int) wrote);
                return false;
        }

        return true;
}

static void
main_thread_wakeup_cb(void *user_data)
{
        struct harness *harness = user_data;

        harness->idle_queued = true;
}

static bool
wait_for_idle_queue_no_flush(struct harness *harness)
{
        /* Wait for up to a second to give the worker thread some time
         * to queue an idle event.
         */
        for (int i = 0; i < 200; i++) {
                if (harness->idle_queued)
                        goto idle_queued;

                struct timespec sleep_time = {
                        .tv_sec = 0,
                        .tv_nsec = 5 * 1000 * 1000, /* 5ms */
                };
                nanosleep(&sleep_time, NULL /* rem */);
        }

        fprintf(stderr,
                "The game state didn’t queue an idle event when one was "
                "expected\n");
        return false;

idle_queued:
        harness->idle_queued = false;

        return true;
}

static bool
wait_for_idle_queue(struct harness *harness)
{
        if (!wait_for_idle_queue_no_flush(harness))
                return false;

        vsx_main_thread_flush_idle_events();

        return true;
}

static void
check_event_cb(struct vsx_listener *listener, void *data)
{
        struct check_event_listener *ce_listener =
                vsx_container_of(listener,
                                 struct check_event_listener,
                                 event_listener);
        const struct vsx_connection_event *event = data;

        if (ce_listener->setup.event_cb == NULL) {
                fprintf(stderr,
                        "Connection event received when none "
                        "was expected\n");
                ce_listener->succeeded = false;
        } else if (ce_listener->setup.expected_event_type != event->type) {
                fprintf(stderr,
                        "Expected event type %i but received %i\n",
                        ce_listener->setup.expected_event_type,
                        event->type);
                ce_listener->succeeded = false;
        } else if (!ce_listener->setup.event_cb(ce_listener->harness,
                                                event,
                                                ce_listener->user_data)) {
                ce_listener->succeeded = false;
        } else {
                ce_listener->setup.event_cb = NULL;
        }
}

static void
check_modified_cb(struct vsx_listener *listener, void *data)
{
        struct check_event_listener *ce_listener =
                vsx_container_of(listener,
                                 struct check_event_listener,
                                 modified_listener);
        const struct vsx_game_state_modified_event *event = data;

        if (ce_listener->setup.modified_cb == NULL) {
                fprintf(stderr,
                        "Modified event received when none "
                        "was expected\n");
                ce_listener->succeeded = false;
        } else if (ce_listener->setup.expected_modified_type != event->type) {
                fprintf(stderr,
                        "Expected modified event type %i but received %i\n",
                        ce_listener->setup.expected_modified_type,
                        event->type);
                ce_listener->succeeded = false;
        } else if (!ce_listener->setup.modified_cb(ce_listener->harness,
                                                   event,
                                                   ce_listener->user_data)) {
                ce_listener->succeeded = false;
        } else {
                ce_listener->setup.modified_cb = NULL;
        }
}

static bool
check_event_or_modified(struct harness *harness,
                        const struct check_event_setup *setup,
                        const uint8_t *data,
                        size_t data_len,
                        void *user_data)
{
        struct check_event_listener listener = {
                .setup = *setup,
                .event_listener = { .notify = check_event_cb },
                .modified_listener = { .notify = check_modified_cb },
                .succeeded = true,
                .harness = harness,
                .user_data = user_data,
        };

        bool ret = true;

        if (setup->event_cb) {
                struct vsx_signal *signal =
                        vsx_game_state_get_event_signal(harness->game_state);
                vsx_signal_add(signal, &listener.event_listener);
        }

        if (setup->modified_cb) {
                struct vsx_signal *signal =
                        vsx_game_state_get_modified_signal(harness->game_state);
                vsx_signal_add(signal, &listener.modified_listener);
        }

        if (!write_data(harness, data, data_len)) {
                ret = false;
                goto out;
        }

        if (!wait_for_idle_queue(harness)) {
                ret = false;
                goto out;
        }

        if (!listener.succeeded) {
                ret = false;
                goto out;
        }

        if (setup->event_cb && listener.setup.event_cb) {
                fprintf(stderr,
                        "No vsx_connection event received when one was "
                        "expected\n");
                ret = false;
                goto out;
        }

        if (setup->modified_cb && listener.setup.modified_cb) {
                fprintf(stderr,
                        "No modified event received when one was "
                        "expected\n");
                ret = false;
                goto out;
        }

out:
        if (setup->event_cb)
                vsx_list_remove(&listener.event_listener.link);
        if (setup->modified_cb)
                vsx_list_remove(&listener.modified_listener.link);

        return ret;
}

static bool
check_event(struct harness *harness,
            enum vsx_connection_event_type expected_type,
            check_event_func event_cb,
            const uint8_t *data,
            size_t data_len,
            void *user_data)
{
        struct check_event_setup setup = {
                .event_cb = event_cb,
                .expected_event_type = expected_type,
        };

        return check_event_or_modified(harness,
                                       &setup,
                                       data,
                                       data_len,
                                       user_data);
}

static bool
check_modified(struct harness *harness,
               enum vsx_game_state_modified_type expected_type,
               check_modified_func modified_cb,
               const uint8_t *data,
               size_t data_len,
               void *user_data)
{
        struct check_event_setup setup = {
                .modified_cb = modified_cb,
                .expected_modified_type = expected_type,
        };

        return check_event_or_modified(harness,
                                       &setup,
                                       data,
                                       data_len,
                                       user_data);
}

struct check_no_modification_closure {
        bool succeeded;
        struct vsx_listener listener;
};

static void
check_no_modification_cb(struct vsx_listener *listener,
                         void *user_data)
{
        struct check_no_modification_closure *closure =
                vsx_container_of(listener,
                                 struct check_no_modification_closure,
                                 listener);
        struct vsx_game_state_modified_event *event = user_data;

        fprintf(stderr,
                "Received modification event %i when none was expected.\n",
                event->type);

        closure->succeeded = false;
}

static bool
check_no_modification(struct harness *harness,
                      const uint8_t *data,
                      size_t data_len)
{
        struct check_no_modification_closure closure = {
                .succeeded = true,
                .listener = {
                        .notify = check_no_modification_cb,
                },
        };

        vsx_signal_add(vsx_game_state_get_modified_signal(harness->game_state),
                       &closure.listener);

        if (!write_data(harness, data, data_len)) {
                closure.succeeded = false;
                goto out;
        }

        if (!wait_for_idle_queue(harness)) {
                closure.succeeded = false;
                goto out;
        }

out:
        vsx_list_remove(&closure.listener.link);

        return closure.succeeded;
}

static bool
check_started_running_cb(struct harness *harness,
                         const struct vsx_connection_event *event,
                         void *user_data)
{
        if (!event->running_state_changed.running) {
                fprintf(stderr,
                        "Running state changed event reported that connection "
                        "is not running\n");
                return false;
        }

        return true;
}

static bool
start_harness(struct harness *harness)
{
        vsx_worker_lock(harness->worker);
        vsx_connection_set_running(harness->connection, true);
        vsx_worker_unlock(harness->worker);

        harness->server_fd = accept(harness->server_sock,
                                    NULL, /* addr */
                                    NULL /* addrlen */);

        if (harness->server_fd == -1) {
                fprintf(stderr,
                        "accept failed: %s\n",
                        strerror(errno));
                return false;
        }

        if (!check_event(harness,
                         VSX_CONNECTION_EVENT_TYPE_RUNNING_STATE_CHANGED,
                         check_started_running_cb,
                         (const uint8_t *) "", 0,
                         NULL /* user_data */))
            return false;

        return true;
}

static struct harness *
create_harness_no_start(void)
{
        struct harness *harness = vsx_calloc(sizeof *harness);

        harness->server_fd = -1;

        vsx_main_thread_set_wakeup_func(main_thread_wakeup_cb, harness);

        harness->server_sock = socket(PF_INET, SOCK_STREAM, 0);

        if (harness->server_sock == -1) {
                fprintf(stderr,
                        "error creating socket: %s\n",
                        strerror(errno));
                goto error;
        }

        const int true_value = true;

        setsockopt(harness->server_sock,
                   SOL_SOCKET, SO_REUSEADDR,
                   &true_value, sizeof true_value);

        struct vsx_netaddress local_address;

        if (!vsx_netaddress_from_string(&local_address,
                                        "127.0.0.1",
                                        TEST_PORT)) {
                fprintf(stderr, "error getting localhost address\n");
                goto error;
        }

        struct vsx_netaddress_native native_local_address;

        vsx_netaddress_to_native(&local_address, &native_local_address);

        if (bind(harness->server_sock,
                 &native_local_address.sockaddr,
                 native_local_address.length) == -1) {
                fprintf(stderr,
                        "error binding server socket: %s\n",
                        strerror(errno));
                goto error;
        }

        if (listen(harness->server_sock, 10) == -1) {
                fprintf(stderr,
                        "listen failed: %s\n",
                        strerror(errno));
                goto error;
        }

        harness->connection = vsx_connection_new();
        vsx_connection_set_room(harness->connection, "test_room");
        vsx_connection_set_player_name(harness->connection, "test_player");
        vsx_connection_set_address(harness->connection, &local_address);

        struct vsx_error *error = NULL;

        harness->worker = vsx_worker_new(harness->connection, &error);

        if (harness->worker == NULL) {
                fprintf(stderr,
                        "Failed to create worker: %s\n",
                        error->message);
                vsx_error_free(error);
                goto error;
        }

        harness->game_state = vsx_game_state_new(harness->worker,
                                                 harness->connection);

        return harness;

error:
        free_harness(harness);
        return NULL;
}

static struct harness *
create_harness(void)
{
        struct harness *harness = create_harness_no_start();

        if (harness == NULL)
                return NULL;

        if (!start_harness(harness)) {
                free_harness(harness);
                return NULL;
        }

        return harness;
}

static void
dump_data(const uint8_t *data, size_t data_len)
{
        for (const uint8_t *p = data; p - data < data_len; p++) {
                if (*p < 32 || *p >= 0x80)
                        fprintf(stderr, "\\x%02x", *p);
                else
                        fputc(*p, stderr);
        }
}

static bool
expect_data(struct harness *harness,
            const uint8_t *data,
            size_t data_len)
{
        uint8_t *buf = vsx_alloc(data_len);

        ssize_t got = read(harness->server_fd, buf, data_len);

        bool ret = true;

        if (got == -1) {
                fprintf(stderr,
                        "Error reading connection: %s\n",
                        strerror(errno));
                ret = false;
        } else if (got != data_len || memcmp(data, buf, data_len)) {
                fprintf(stderr,
                        "Data read from client does not match expected\n"
                        "Expected:\n");
                dump_data(data, data_len);
                fprintf(stderr,
                        "\n"
                        "Received:\n");
                dump_data(buf, got);
                fputc('\n', stderr);
                ret = false;
        }

        vsx_free(buf);

        return ret;
}

static bool
read_ws_request(struct harness *harness)
{
        static const uint8_t ws_request[] =
                "GET / HTTP/1.1\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                "\r\n";

        return expect_data(harness, ws_request, sizeof ws_request - 1);
}

static bool
read_new_player_request(struct harness *harness)
{
        static const uint8_t new_player_request[] =
                "\x82\x17\x80test_room\0test_player\0";

        return expect_data(harness,
                           new_player_request,
                           sizeof new_player_request - 1);
}

static bool
check_header_cb(struct harness *harness,
                const struct vsx_connection_event *event,
                void *user_data)
{
        if (event->header.self_num != 0) {
                fprintf(stderr,
                        "Expected self to be 0 in header but got %i\n",
                        event->header.self_num);
                return false;
        }

        const uint64_t expected_id = UINT64_C(0x6e6d6c6b6a696867);

        if (event->header.person_id != expected_id) {
                fprintf(stderr,
                        "person_id does not match in header\n"
                        " Expected: 0x%" PRIx64 "\n"
                        " Received: 0x%" PRIx64 "\n",
                        expected_id,
                        event->header.person_id);
                return false;
        }

        return true;
}

static bool
send_player_id(struct harness *harness)
{
        static const uint8_t player_id_header[] = "\x82\x0a\x00ghijklmn\x00";

        if (!check_event(harness,
                         VSX_CONNECTION_EVENT_TYPE_HEADER,
                         check_header_cb,
                         player_id_header,
                         sizeof player_id_header - 1,
                         NULL /* user_data */))
                return false;

        if (vsx_game_state_get_self(harness->game_state) != 0) {
                fprintf(stderr,
                        "self is not 0 (=%i)\n",
                        vsx_game_state_get_self(harness->game_state));
                return false;
        }

        return true;
}

static bool
negotiate_harness(struct harness *harness)
{
        if (!read_ws_request(harness))
                return false;

        if (!write_data(harness, (const uint8_t *) "\r\n\r\n", 4))
                return false;

        if (!read_new_player_request(harness))
                return false;

        if (!send_player_id(harness))
                return false;

        return true;
}

static struct harness *
create_negotiated_harness(void)
{
        struct harness *harness = create_harness();

        if (harness == NULL)
                return NULL;

        if (!negotiate_harness(harness)) {
                free_harness(harness);
                return NULL;
        }

        return harness;
}

struct send_tile_closure {
        int num;
        int x;
        int y;
        char letter;
};

static bool
check_tile_changed_cb(struct harness *harness,
                      const struct vsx_connection_event *event,
                      void *user_data)
{
        struct send_tile_closure *closure = user_data;

        if (event->tile_changed.num != closure->num ||
            event->tile_changed.x != closure->x ||
            event->tile_changed.y != closure->y ||
            event->tile_changed.letter != closure->letter) {
                fprintf(stderr,
                        "Tile from event does not match sent tile:\n"
                        " Expected: %i %i,%i %c\n"
                        " Received: %i %i,%i %c\n",
                        closure->num,
                        closure->x,
                        closure->y,
                        closure->letter,
                        event->tile_changed.num,
                        event->tile_changed.x,
                        event->tile_changed.y,
                        event->tile_changed.letter);
                return false;
        }

        return true;
}

static bool
send_tile(struct harness *harness,
          int num,
          int x,
          int y,
          char letter,
          uint8_t player)
{
        uint8_t add_tile_message[] =
                "\x82\x09\x03\x00\x01\x00\x02\x00g\x00\x00";

        add_tile_message[3] = num;
        add_tile_message[4] = x;
        add_tile_message[5] = x >> 8;
        add_tile_message[6] = y;
        add_tile_message[7] = y >> 8;
        add_tile_message[8] = letter;
        add_tile_message[10] = player;

        struct send_tile_closure closure = {
                .num = num,
                .x = x,
                .y = y,
                .letter = letter,
        };

        return check_event(harness,
                           VSX_CONNECTION_EVENT_TYPE_TILE_CHANGED,
                           check_tile_changed_cb,
                           add_tile_message,
                           sizeof add_tile_message - 1,
                           &closure);
}

struct check_tiles_closure {
        struct harness *harness;
        vsx_bitmask_element_t sent_tiles[VSX_BITMASK_N_ELEMENTS_FOR_SIZE(256)];
        bool succeeded;
};

static void
check_tiles_cb(const struct vsx_connection_event *event,
               void *user_data)
{
        struct check_tiles_closure *closure = user_data;
        int tile_num = event->tile_changed.num;

        if (tile_num < 0 || tile_num > 255) {
                fprintf(stderr,
                        "Invalid tile number received: %i\n",
                        tile_num);
                closure->succeeded = false;
                return;
        }

        if (vsx_bitmask_get(closure->sent_tiles, tile_num)) {
                fprintf(stderr,
                        "Tile number %i sent more than once\n",
                        tile_num);
                closure->succeeded = false;
                return;
        }

        vsx_bitmask_set(closure->sent_tiles, tile_num, true);

        int16_t x = tile_num * 257;
        int y = (tile_num & 1) ? -tile_num : tile_num;

        if (x != event->tile_changed.x || y != event->tile_changed.y) {
                fprintf(stderr,
                        "Wrong tile position reported.\n"
                        " Expected: %i,%i\n"
                        " Received: %i,%i\n",
                        x, y,
                        event->tile_changed.x, event->tile_changed.y);
                closure->succeeded = false;
                return;
        }

        char letter = tile_num % 26 + 'A';

        if (letter != event->tile_changed.letter) {
                fprintf(stderr,
                        "Reported tile letter does not match. (%c != %c)\n",
                        letter,
                        event->tile_changed.letter);
                closure->succeeded = false;
                return;
        }
}

static bool
test_send_all_tiles(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        bool ret = true;

        /* Add all of the possible tiles */
        for (int i = 0; i < 256; i++) {
                /* Send them in a strange order */
                int tile_num = ((i & 0xfc) |
                                ((i & 2) >> 1) |
                                ((i & 1) << 1));

                int x = tile_num * 257;
                if ((x & 0x8000))
                        x |= -1 & ~0xffff;

                if (!send_tile(harness,
                               tile_num,
                               x,
                               (tile_num & 1) ? -tile_num : tile_num,
                               tile_num % 26 + 'A',
                               tile_num / 2)) {
                        ret = false;
                        goto out;
                }
        }

        /* Update one of the tiles */
        if (!send_tile(harness,
                       1,
                       257,
                       -1,
                       'B',
                       0)) {
                ret = false;
                goto out;
        }

        struct check_tiles_closure closure = {
                .harness = harness,
                .succeeded = true,
        };

        memset(closure.sent_tiles, 0, sizeof closure.sent_tiles);

        vsx_game_state_foreach_tile(harness->game_state,
                                    check_tiles_cb,
                                    &closure);

        if (!closure.succeeded) {
                ret = false;
                goto out;
        }

        for (int i = 0; i < 256; i++) {
                if (!vsx_bitmask_get(closure.sent_tiles, i)) {
                        fprintf(stderr,
                                "vsx_game_state_foreach_tile didn’t report "
                                "tile %i\n",
                                i);
                        ret = false;
                        goto out;
                }
        }

out:
        free_harness(harness);

        return ret;
}

struct check_players_closure {
        struct harness *harness;
        int next_player_num;
        bool succeeded;
};

static void
check_players_cb(const char *name,
                 enum vsx_game_state_player_flag flags,
                 void *user_data)
{
        struct check_players_closure *closure = user_data;
        int player_num = closure->next_player_num++;

        struct vsx_buffer buf = VSX_BUFFER_STATIC_INIT;

        if (player_num == 1)
                vsx_buffer_append_string(&buf, "George");
        else
                vsx_buffer_append_printf(&buf, "Player %i", player_num);

        if (strcmp(name, (const char *) buf.data)) {
                fprintf(stderr,
                        "Wrong player name reported.\n"
                        " Expected: %s\n"
                        " Received: %s\n",
                        (const char *) buf.data,
                        name);
                closure->succeeded = false;
        }

        int expected_flags = player_num & 0x3;

        if (expected_flags != flags) {
                fprintf(stderr,
                        "Wrong flags reported.\n"
                        " Expected: 0x%x\n"
                        " Received: 0x%x\n",
                        expected_flags,
                        flags);
                closure->succeeded = false;
        }

        vsx_buffer_destroy(&buf);
}

static bool
check_player_added_cb(struct harness *harness,
                      const struct vsx_connection_event *event,
                      void *user_data)
{
        if (event->player_name_changed.player_num != 1) {
                fprintf(stderr,
                        "Expected other player to have number 1 but got %i\n",
                        event->player_name_changed.player_num);
                return false;
        }

        if (strcmp(event->player_name_changed.name, "George")) {
                fprintf(stderr,
                        "Other player is not called George: %s\n",
                        event->player_name_changed.name);
                return false;
        }

        return true;
}

static bool
check_player_name_changed_cb(struct harness *harness,
                             const struct vsx_game_state_modified_event *event,
                             void *user_data)
{
        if (event->player_name.player_num != 1) {
                fprintf(stderr,
                        "Wrong player changed.\n"
                        " Expected: %i\n"
                        " Received: %i\n",
                        1,
                        event->player_name.player_num);
                return false;
        }

        if (strcmp(event->player_name.name, "George")) {
                fprintf(stderr,
                        "Wrong player name.\n"
                        " Expected: George\n"
                        " Received: %s\n",
                        event->player_name.name);
                return false;
        }

        return true;
}

static bool
add_player(struct harness *harness)
{
        const struct check_event_setup setup = {
                .event_cb = check_player_added_cb,
                .expected_event_type =
                VSX_CONNECTION_EVENT_TYPE_PLAYER_NAME_CHANGED,
                .modified_cb = check_player_name_changed_cb,
                .expected_modified_type =
                VSX_GAME_STATE_MODIFIED_TYPE_PLAYER_NAME,
        };

        static const uint8_t add_player_message[] =
                "\x82\x09\x04\x01George\x00";

        return check_event_or_modified(harness,
                                       &setup,
                                       add_player_message,
                                       sizeof add_player_message - 1,
                                       NULL /* user_data */);
}

struct check_player_added_closure {
        int player_num;
};

static bool
check_player_name_added_cb(struct harness *harness,
                           const struct vsx_connection_event *event,
                           void *user_data)
{
        struct check_player_added_closure *closure = user_data;

        struct vsx_buffer buf = VSX_BUFFER_STATIC_INIT;
        bool ret = true;

        vsx_buffer_append_printf(&buf, "Player %i", closure->player_num);

        if (strcmp((char *) buf.data, event->player_name_changed.name)) {
                fprintf(stderr,
                        "Player name different\n"
                        " Expected: %s\n"
                        " Received: %s\n",
                        (const char *) buf.data,
                        event->player_name_changed.name);
                ret = false;
                goto out;
        }

        if (event->player_name_changed.player_num != closure->player_num) {
                fprintf(stderr,
                        "Expected name change event for %i but got %i\n",
                        closure->player_num,
                        event->player_name_changed.player_num);
                ret = false;
                goto out;
        }

out:
        vsx_buffer_destroy(&buf);
        return ret;
}

static bool
check_player_flags_added_cb(struct harness *harness,
                            const struct vsx_connection_event *event,
                            void *user_data)
{
        struct check_player_added_closure *closure = user_data;

        if (event->player_flags_changed.player_num != closure->player_num) {
                fprintf(stderr,
                        "Expected flags changed event for %i but got %i\n",
                        closure->player_num,
                        event->player_flags_changed.player_num);
                return false;
        }

        int expected_flags = closure->player_num & 0x3;

        if (event->player_flags_changed.flags != expected_flags) {
                fprintf(stderr,
                        "Expected flags to be 0x%x but got 0x%x\n",
                        expected_flags,
                        event->player_flags_changed.flags);
                return false;
        }

        return true;
}

static bool
test_send_all_players(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        bool ret = true;
        struct vsx_buffer buf = VSX_BUFFER_STATIC_INIT;

        /* Add all of the possible players */
        for (int i = 0; i < 256; i++) {
                /* Send them in a strange order */
                int player_num = i ^ 1;

                vsx_buffer_set_length(&buf, 0);
                vsx_buffer_append_string(&buf, "\x82\xff\x04\xff");
                vsx_buffer_append_printf(&buf, "Player %i", player_num);
                buf.length++;
                buf.data[1] = buf.length - 2;
                buf.data[3] = player_num;

                struct check_player_added_closure closure = {
                        .player_num = player_num,
                };

                if (!check_event(harness,
                                 VSX_CONNECTION_EVENT_TYPE_PLAYER_NAME_CHANGED,
                                 check_player_name_added_cb,
                                 buf.data, buf.length,
                                 &closure)) {
                        ret = false;
                        goto out;
                }

                vsx_buffer_set_length(&buf, 0);
                vsx_buffer_append_string(&buf, "\x82\x03\x05");
                vsx_buffer_append_c(&buf, player_num);
                vsx_buffer_append_c(&buf, player_num & 0x3);

                if (!check_event(harness,
                                 VSX_CONNECTION_EVENT_TYPE_PLAYER_FLAGS_CHANGED,
                                 check_player_flags_added_cb,
                                 buf.data, buf.length,
                                 &closure)) {
                        ret = false;
                        goto out;
                }
        }

        /* Update one of the players */
        if (!add_player(harness)) {
                ret = false;
                goto out;
        }

        struct check_players_closure closure = {
                .harness = harness,
                .next_player_num = 0,
                .succeeded = true,
        };

        vsx_game_state_foreach_player(harness->game_state,
                                      check_players_cb,
                                      &closure);

        if (!closure.succeeded) {
                ret = false;
                goto out;
        }

        if (closure.next_player_num != VSX_GAME_STATE_N_VISIBLE_PLAYERS) {
                fprintf(stderr,
                        "vsx_game_state_foreach_player didn’t report "
                        "all the players\n");
                ret = false;
                goto out;
        }

out:
        vsx_buffer_destroy(&buf);
        free_harness(harness);

        return ret;
}

struct check_shouting_closure {
        int clear_shouting_player;
        struct vsx_listener event_listener;
        struct vsx_listener modified_listener;
        bool got_modified_event;
        bool got_player_shouted_event;
        bool succeeded;
};

static void
check_shouting_cb(struct vsx_listener *listener,
                  void *user_data)
{
        struct check_shouting_closure *closure =
                vsx_container_of(listener,
                                 struct check_shouting_closure,
                                 event_listener);
        const struct vsx_connection_event *event = user_data;

        if (event->type != VSX_CONNECTION_EVENT_TYPE_PLAYER_SHOUTED) {
                fprintf(stderr,
                        "Received unexpected event %i after setting player "
                        "shouting.\n",
                        event->type);
                closure->succeeded = false;
                return;
        }

        if (closure->got_player_shouted_event) {
                fprintf(stderr,
                        "Received multiple PLAYER_SHOUTED events\n");
                        closure->succeeded = false;
                        return;
        }

        closure->got_player_shouted_event = true;
}

static void
check_shouting_modified_cb(struct vsx_listener *listener,
                           void *user_data)
{
        struct check_shouting_closure *closure =
                vsx_container_of(listener,
                                 struct check_shouting_closure,
                                 modified_listener);
        const struct vsx_game_state_modified_event *event = user_data;

        if (event->type != VSX_GAME_STATE_MODIFIED_TYPE_SHOUTING_PLAYER) {
                fprintf(stderr,
                        "Received unexpected modified event %i "
                        "after setting player shouting.\n",
                        event->type);
                closure->succeeded = false;
                return;
        }

        closure->got_modified_event = true;
}

static bool
check_shouting_events(struct harness *harness,
                      int set_player_num,
                      int clear_player_num)
{
        struct check_shouting_closure closure = {
                .succeeded = true,
                .got_player_shouted_event = false,
                .got_modified_event = false,
                .event_listener = {
                        .notify = check_shouting_cb,
                },
                .modified_listener = {
                        .notify = check_shouting_modified_cb,
                },
        };

        vsx_signal_add(vsx_game_state_get_event_signal(harness->game_state),
                       &closure.event_listener);
        vsx_signal_add(vsx_game_state_get_modified_signal(harness->game_state),
                       &closure.modified_listener);

        bool ret = wait_for_idle_queue(harness);

        vsx_list_remove(&closure.event_listener.link);
        vsx_list_remove(&closure.modified_listener.link);

        if (!ret || !closure.succeeded)
                return false;

        if (set_player_num >= 0 &&
            !closure.got_player_shouted_event) {
                fprintf(stderr, "No player shouted event received.\n");
                return false;
        }

        if (closure.got_modified_event &&
            set_player_num == -1 &&
            clear_player_num == -1) {
                fprintf(stderr,
                        "Got a shouting player modified event when nothing "
                        "should have changed.\n");
                return false;
        }

        if (!closure.got_modified_event &&
            (set_player_num >= 0 || clear_player_num >= 0)) {
                fprintf(stderr,
                        "No modified event received for shouting change.\n");
                return false;
        }

        return true;
}

static bool
send_shout(struct harness *harness,
           int player_num,
           int clear_player_num)
{
        uint8_t message[4] = "\x82\x02\x06\x00";
        message[3] = player_num;

        if (!write_data(harness, message, sizeof message))
                return false;

        if (!check_shouting_events(harness, player_num, clear_player_num))
                return false;

        int actual_shouting_player =
                vsx_game_state_get_shouting_player(harness->game_state);

        if (player_num != actual_shouting_player) {
                fprintf(stderr,
                        "Shouting player does not match expected.\n"
                        " Expected: %i\n"
                        " Received: %i\n",
                        player_num,
                        actual_shouting_player);
                return false;
        }

        return true;
}

static bool
test_shouting(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        bool ret = true;

        if (!add_player(harness)) {
                ret = false;
                goto out;
        }

        if (!send_shout(harness,
                        1, /* player_num */
                        -1 /* clear_player_num */)) {
                ret = false;
                goto out;
        }

        /* Send the same shout again, this shouln’t trigger a modified event */
        if (!write_data(harness,
                        (const uint8_t *) "\x82\x02\x06\x01",
                        4)) {
                ret = false;
                goto out;
        }

        /* Check that no modification event was triggered */
        if (!check_shouting_events(harness, -1, -1)) {
                ret = false;
                goto out;
        }

        int64_t shout_start_time = vsx_monotonic_get();

        if (!send_shout(harness,
                        0, /* player_num */
                        1 /* clear_player_num */)) {
                ret = false;
                goto out;
        }

        struct timespec sleep_time = {
                .tv_sec = 9,
                .tv_nsec = 500 * 1000 * 1000, /* 500ms */
        };
        nanosleep(&sleep_time, NULL /* rem */);

        vsx_main_thread_flush_idle_events();

        int actual_shouting_player =
                vsx_game_state_get_shouting_player(harness->game_state);

        if (actual_shouting_player != 0) {
                fprintf(stderr,
                        "Shouting player after 9.5 seconds is wrong "
                        "(%i != %i)\n",
                        0,
                        actual_shouting_player);
                ret = false;
                goto out;
        }

        /* This should wait long enough to see the shout clear event */
        if (!check_shouting_events(harness,
                                   -1, /* set_player_num */
                                   0 /* clear_player_num */)) {
                ret = false;
                goto out;
        }

        actual_shouting_player =
                vsx_game_state_get_shouting_player(harness->game_state);

        if (actual_shouting_player != -1) {
                fprintf(stderr,
                        "Shouting player after clear shout is wrong "
                        "(%i != %i)\n",
                        -1,
                        actual_shouting_player);
                ret = false;
                goto out;
        }

        float delay = (vsx_monotonic_get() - shout_start_time) / 1.0E6f;

        if (fabsf(delay - 10.0f) >= 0.5f) {
                fprintf(stderr,
                        "Expected shout to be cleared after 10 seconds but it "
                        "took %f\n",
                        delay);
                ret = false;
                goto out;
        }

out:
        free_harness(harness);
        return ret;
}

static bool
test_non_visible_shouting(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        bool ret = true;

        static const uint8_t add_players_message[] =
                "\x82\x04\x04\x01g\x00"
                "\x82\x04\x04\x02h\x00"
                "\x82\x04\x04\x03i\x00"
                "\x82\x04\x04\x04j\x00"
                "\x82\x04\x04\x05k\x00"
                "\x82\x04\x04\x06l\x00";

        if (!write_data(harness,
                        add_players_message,
                        (sizeof add_players_message) - 1)) {
                ret = false;
                goto out;
        }

        /* Ignore the messages */
        if (!wait_for_idle_queue(harness)) {
                ret = false;
                goto out;
        }

        if (!send_shout(harness,
                        6, /* player_num */
                        -1 /* clear_player_num */)) {
                ret = false;
                goto out;
        }

        if (!send_shout(harness,
                        1, /* player_num */
                        6 /* clear_player_num */)) {
                ret = false;
                goto out;
        }

out:
        free_harness(harness);
        return ret;
}

static bool
test_send_commands(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        bool ret = true;

        vsx_game_state_shout(harness->game_state);

        if (!expect_data(harness, (const uint8_t *) "\x82\x01\x8a", 3)) {
                ret = false;
                goto out;
        }

        vsx_game_state_turn(harness->game_state);

        if (!expect_data(harness, (const uint8_t *) "\x82\x01\x89", 3)) {
                ret = false;
                goto out;
        }

        vsx_game_state_move_tile(harness->game_state,
                                 5, /* tile_num */
                                 4, 2 /* x/y */);

        if (!expect_data(harness,
                         (const uint8_t *)
                         "\x82\x06\x88\x05\x04\x00\x02\x00",
                         8)) {
                ret = false;
                goto out;
        }

        vsx_game_state_set_n_tiles(harness->game_state, 10);

        if (!expect_data(harness,
                         (const uint8_t *)
                         "\x82\x02\x8b\x0a",
                         4)) {
                ret = false;
                goto out;
        }

out:
        free_harness(harness);
        return ret;
}

static bool
check_conversation_id_modified_cb(struct harness *harness,
                                  const struct vsx_game_state_modified_event *e,
                                  void *user_data)
{
        return true;
}

static bool
test_conversation_id(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        bool ret = true;

        uint64_t conversation_id;

        if (vsx_game_state_get_conversation_id(harness->game_state,
                                               &conversation_id)) {
                fprintf(stderr,
                        "The game state has a conversation ID before one was "
                        "sent.\n");
                ret = false;
                goto out;
        }

        static const uint8_t conversation_id_message[] =
                "\x82\x09\x0a\x81\x82\x83\x84\x85\x86\x87\x88";

        if (!check_modified(harness,
                            VSX_GAME_STATE_MODIFIED_TYPE_CONVERSATION_ID,
                            check_conversation_id_modified_cb,
                            conversation_id_message,
                            sizeof conversation_id_message - 1,
                            NULL /* user_data */)) {
                ret = false;
                goto out;
        }

        if (!vsx_game_state_get_conversation_id(harness->game_state,
                                                &conversation_id)) {
                fprintf(stderr,
                        "The game state doesn’t have a conversation ID even "
                        "after one was sent.\n");
                ret = false;
                goto out;
        }

        uint64_t expected_id = UINT64_C(0x8887868584838281);

        if (expected_id != conversation_id) {
                fprintf(stderr,
                        "Game state conversation id does not match.\n"
                        " Expected: 0x%" PRIx64 "\n"
                        " Received: 0x%" PRIx64 "\n",
                        expected_id,
                        conversation_id);
                ret = false;
                goto out;
        }

        /* Send the same message again and verify that it doesn’t emit
         * a modification event.
         */
        if (!check_no_modification(harness,
                                   conversation_id_message,
                                   sizeof conversation_id_message - 1)) {
                ret = false;
                goto out;
        }

out:
        free_harness(harness);
        return ret;
}

static bool
check_n_tiles_modified_cb(struct harness *harness,
                          const struct vsx_game_state_modified_event *event,
                          void *user_data)
{
        int n_tiles = vsx_game_state_get_n_tiles(harness->game_state);

        if (n_tiles != 5) {
                fprintf(stderr,
                        "Expected n_tiles to be 5 but got %i\n",
                        n_tiles);
                return false;
        }

        return true;
}

static bool
test_n_tiles(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        bool ret = true;

        static const uint8_t n_tiles_message[] =
                "\x82\x02\x02\x05";

        if (!check_modified(harness,
                            VSX_GAME_STATE_MODIFIED_TYPE_N_TILES,
                            check_n_tiles_modified_cb,
                            n_tiles_message,
                            sizeof n_tiles_message - 1,
                            NULL /* user_data */)) {
                ret = false;
                goto out;
        }

        /* Send the same message again and verify that it doesn’t emit
         * a modification event.
         */
        if (!check_no_modification(harness,
                                   n_tiles_message,
                                   sizeof n_tiles_message - 1)) {
                ret = false;
                goto out;
        }

out:
        free_harness(harness);
        return ret;
}

static bool
test_dangling_events(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        bool ret = true;

        /* Update n_tiles so that the game state will queue an event */
        if (!write_data(harness, (const uint8_t *) "\x82\x02\x02\x10", 4) ||
            !wait_for_idle_queue_no_flush(harness)) {
                ret = false;
                goto out;
        }

        /* Now let the harness be freed before the game state gets a
         * chance to emit the event.
         */

out:
        free_harness(harness);
        return ret;
}

static bool
test_self(void)
{
        struct harness *harness = create_harness();

        if (harness == NULL)
                return NULL;

        bool ret = true;

        if (!read_ws_request(harness) ||
            !write_data(harness, (const uint8_t *) "\r\n\r\n", 4) ||
            !read_new_player_request(harness)) {
                ret = false;
                goto out;
        }

        if (!write_data(harness,
                        (const uint8_t *)
                        "\x82\x0a\x00ghijklmn\x10",
                        12)) {
                ret = false;
                goto out;
        }

        if (!wait_for_idle_queue(harness)) {
                ret = false;
                goto out;
        }

        if (vsx_game_state_get_self(harness->game_state) != 16) {
                fprintf(stderr,
                        "unexpected self value.\n"
                        " Expected: 16\n"
                        " Received: %i\n",
                        vsx_game_state_get_self(harness->game_state));
                ret = false;
                goto out;
        }

out:
        free_harness(harness);
        return ret;
}

static bool
test_load_instance_state(void)
{
        struct harness *harness = create_harness_no_start();

        if (harness == NULL)
                return false;

        bool ret = true;

        vsx_game_state_load_instance_state(harness->game_state,
                                           "person_id=5,dialog=none");

        enum vsx_dialog dialog = vsx_game_state_get_dialog(harness->game_state);

        if (dialog != VSX_DIALOG_NONE) {
                fprintf(stderr,
                        "Dialog not as expected after loading a state.\n"
                        " Expected: %i (%s)\n"
                        " Got: %i (%s)\n",
                        VSX_DIALOG_NONE,
                        vsx_dialog_to_name(VSX_DIALOG_NONE),
                        dialog,
                        vsx_dialog_to_name(dialog));
                ret = false;
                goto out;
        }

        if (!start_harness(harness)) {
                ret = false;
                goto out;
        }

        if (!read_ws_request(harness) ||
            !write_data(harness, (const uint8_t *) "\r\n\r\n", 4)) {
                ret = false;
                goto out;
        }

        static const uint8_t reconnect_request[] =
                "\x82\x0b\x81\x05\x00\x00\x00\x00\x00\x00\x00\x00\x00";

        /* Check that we get a reconnect message with the player ID
         * that we loaded from the instance state.
         */
        if (!expect_data(harness,
                         reconnect_request,
                         sizeof reconnect_request - 1)) {
                ret = false;
                goto out;
        }

out:
        free_harness(harness);
        return ret;
}

static bool
test_load_empty_instance_state(void)
{
        struct harness *harness = create_harness_no_start();

        if (harness == NULL)
                return false;

        bool ret = true;

        vsx_game_state_load_instance_state(harness->game_state, "");

        /* The string is empty so the connection should start a
         * regular new player request.
         */
        ret = start_harness(harness) && negotiate_harness(harness);

        free_harness(harness);

        return ret;
}

static bool
test_save_instance_state(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        bool ret = true;

        vsx_game_state_set_dialog(harness->game_state, VSX_DIALOG_NONE);

        char *str = vsx_game_state_save_instance_state(harness->game_state);

        const char *expected_str =
                "person_id=6e6d6c6b6a696867,dialog=none";

        if (strcmp(str, expected_str)) {
                fprintf(stderr,
                        "String from saved instance state does not match.\n"
                        " Expected: %s\n"
                        " Received: %s\n",
                        expected_str,
                        str);
                ret = false;
        }

        vsx_free(str);
        free_harness(harness);

        return ret;
}

struct check_player_flags_closure {
        bool succeeded;
        bool found_connected_player;
};

static void
check_player_flags_foreach_player_cb(const char *name,
                                     enum vsx_game_state_player_flag flags,
                                     void *user_data)
{
        struct check_player_flags_closure *closure = user_data;

        if ((flags & VSX_GAME_STATE_PLAYER_FLAG_CONNECTED) == 0)
                return;

        if (closure->found_connected_player) {
                fprintf(stderr,
                        "Found multiple connected players when only one "
                        "expected\n");
                closure->succeeded = false;
        } else {
                closure->found_connected_player = true;

                if (flags != 3) {
                        fprintf(stderr,
                                "Player flags expected to be 3, got %i\n",
                                (int) flags);
                        closure->succeeded = false;
                }
        }
}

static bool
check_player_flags_modified_cb(struct harness *harness,
                               const struct vsx_game_state_modified_event *e,
                               void *user_data)
{
        struct check_player_flags_closure closure = {
                .succeeded = true,
        };

        vsx_game_state_foreach_player(harness->game_state,
                                      check_player_flags_foreach_player_cb,
                                      &closure);

        if (!closure.succeeded)
                return false;

        if (!closure.found_connected_player) {
                fprintf(stderr, "No connected player found\n");
                return false;
        }

        return true;
}

static bool
test_typing_modified(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        bool ret = true;

        static const uint8_t typing_message[] = "\x82\x03\x05\x00\x03";

        /* Set the typing flag for the player and make sure that we
         * get a player flags modified event.
         */
        if (!check_modified(harness,
                            VSX_GAME_STATE_MODIFIED_TYPE_PLAYER_FLAGS,
                            check_player_flags_modified_cb,
                            typing_message,
                            sizeof typing_message - 1,
                            NULL /* user_data */)) {
                ret = false;
                goto out;
        }

        /* Send the same event again and make sure that it doesn’t
         * send another modification event.
         */
        if (!check_no_modification(harness,
                                   typing_message,
                                   sizeof typing_message - 1)) {
                ret = false;
                goto out;
        }

out:
        free_harness(harness);

        return ret;
}

struct test_dialog_closure {
        struct harness *harness;
        bool succeeded;
        enum vsx_dialog expected_value;
        bool had_event;
        struct vsx_listener listener;
};

static void
test_dialog_cb(struct vsx_listener *listener,
               void *user_data)
{
        struct test_dialog_closure *closure =
                vsx_container_of(listener,
                                 struct test_dialog_closure,
                                 listener);
        const struct vsx_game_state_modified_event *event = user_data;

        if (event->type != VSX_GAME_STATE_MODIFIED_TYPE_DIALOG) {
                fprintf(stderr,
                        "Received unexpected modified event %i while setting "
                        "dialog.\n",
                        event->type);
                closure->succeeded = false;
                return;
        }

        if (closure->had_event) {
                fprintf(stderr,
                        "Received multiple dialog modified events.\n");
                closure->succeeded = false;
                return;
        }

        enum vsx_dialog real_value =
                vsx_game_state_get_dialog(closure->harness->game_state);

        if (closure->expected_value != real_value) {
                fprintf(stderr,
                        "dialog has wrong value\n"
                        " Expected: %i (%s)\n"
                        " Received: %i (%s)\n",
                        closure->expected_value,
                        vsx_dialog_to_name(closure->expected_value),
                        real_value,
                        vsx_dialog_to_name(real_value));
                closure->succeeded = false;
                return;
        }

        closure->had_event = true;
}

static bool
test_dialog(void)
{
        struct harness *harness = create_harness_no_start();

        if (!harness)
                return false;

        bool ret = true;

        struct test_dialog_closure closure = {
                .harness = harness,
                .succeeded = true,
                .expected_value = VSX_DIALOG_NONE,
                .had_event = false,
                .listener = {
                        .notify = test_dialog_cb,
                },
        };

        struct vsx_signal *signal =
                vsx_game_state_get_modified_signal(harness->game_state);
        vsx_signal_add(signal, &closure.listener);

        if (vsx_game_state_get_dialog(harness->game_state) !=
            VSX_DIALOG_INVITE_LINK) {
                fprintf(stderr,
                        "dialog didn’t start off as invite_link\n");
                ret = false;
                goto out;
        }

        vsx_game_state_set_dialog(harness->game_state, VSX_DIALOG_NONE);

        if (!closure.succeeded) {
                ret = false;
                goto out;
        }

        if (!closure.had_event) {
                fprintf(stderr,
                        "No modified event received after setting dialog.\n");
                ret = false;
                goto out;
        }

        /* Set the same value again and ensure no event was triggered */

        closure.had_event = false;

        vsx_game_state_set_dialog(harness->game_state, VSX_DIALOG_NONE);

        if (!closure.succeeded) {
                ret = false;
                goto out;
        }

        if (closure.had_event) {
                fprintf(stderr,
                        "A modified event was received after setting dialog "
                        "to same value.\n");
                ret = false;
                goto out;
        }

out:
        vsx_list_remove(&closure.listener.link);
        free_harness(harness);

        return ret;
}

int
main(int argc, char **argv)
{
        int ret = EXIT_SUCCESS;

        if (!test_self())
                ret = EXIT_FAILURE;

        if (!test_load_instance_state())
                ret = EXIT_FAILURE;

        if (!test_load_empty_instance_state())
                ret = EXIT_FAILURE;

        if (!test_save_instance_state())
                ret = EXIT_FAILURE;

        if (!test_typing_modified())
                ret = EXIT_FAILURE;

        if (!test_send_all_tiles())
                ret = EXIT_FAILURE;

        if (!test_send_all_players())
                ret = EXIT_FAILURE;

        if (!test_shouting())
                ret = EXIT_FAILURE;

        if (!test_non_visible_shouting())
                ret = EXIT_FAILURE;

        if (!test_send_commands())
                ret = EXIT_FAILURE;

        if (!test_conversation_id())
                ret = EXIT_FAILURE;

        if (!test_dialog())
                ret = EXIT_FAILURE;

        if (!test_n_tiles())
                ret = EXIT_FAILURE;

        if (!test_dangling_events())
                ret = EXIT_FAILURE;

        /* Flush any pending main thread events to make sure they were
         * all cleaned up
         */
        vsx_main_thread_flush_idle_events();

        vsx_main_thread_clean_up();

        return ret;
}
