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

#include "vsx-connection.h"
#include "vsx-worker.h"
#include "vsx-util.h"
#include "vsx-proto.h"
#include "vsx-game-state.h"
#include "vsx-main-thread.h"
#include "vsx-bitmask.h"

#define TEST_PORT 6138

struct harness {
        int server_sock;
        struct vsx_connection *connection;
        struct vsx_worker *worker;
        struct vsx_game_state *game_state;

        int server_fd;

        bool idle_queued;
};

enum check_event_result {
        CHECK_EVENT_RESULT_NO_MESSAGE,
        CHECK_EVENT_RESULT_FAILED,
        CHECK_EVENT_RESULT_SUCCEEDED,
};

typedef bool
(* check_event_func)(struct harness *harness,
                     const struct vsx_connection_event *event,
                     void *user_data);

struct check_event_listener {
        struct vsx_listener listener;
        enum check_event_result result;
        enum vsx_connection_event_type expected_type;
        check_event_func cb;
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
wait_for_idle_queue(struct harness *harness)
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
        vsx_main_thread_flush_idle_events();

        return true;
}

static void
check_event_cb(struct vsx_listener *listener, void *data)
{
        struct check_event_listener *ce_listener =
                vsx_container_of(listener,
                                 struct check_event_listener,
                                 listener);
        const struct vsx_connection_event *event = data;

        if (ce_listener->result != CHECK_EVENT_RESULT_NO_MESSAGE) {
                fprintf(stderr,
                        "Multiple events received when only one "
                        "was expected\n");
                ce_listener->result = CHECK_EVENT_RESULT_FAILED;
        } else if (ce_listener->expected_type != event->type) {
                fprintf(stderr,
                        "Expected event type %i but received %i\n",
                        ce_listener->expected_type,
                        event->type);
                ce_listener->result = CHECK_EVENT_RESULT_FAILED;
        } else if (ce_listener->cb(ce_listener->harness,
                                   event,
                                   ce_listener->user_data)) {
                ce_listener->result = CHECK_EVENT_RESULT_SUCCEEDED;
        } else {
                ce_listener->result = CHECK_EVENT_RESULT_FAILED;
        }
}

static bool
check_event(struct harness *harness,
            enum vsx_connection_event_type expected_type,
            check_event_func cb,
            const uint8_t *data,
            size_t data_len,
            void *user_data)
{
        struct check_event_listener listener = {
                .listener = { .notify = check_event_cb },
                .result = CHECK_EVENT_RESULT_NO_MESSAGE,
                .expected_type = expected_type,
                .cb = cb,
                .harness = harness,
                .user_data = user_data,
        };

        bool ret = true;

        vsx_signal_add(vsx_game_state_get_event_signal(harness->game_state),
                       &listener.listener);

        if (!write_data(harness, data, data_len)) {
                ret = false;
                goto out;
        }

        if (!wait_for_idle_queue(harness)) {
                ret = false;
                goto out;
        }

        switch (listener.result) {
        case CHECK_EVENT_RESULT_NO_MESSAGE:
                fprintf(stderr, "No event received when one was expected\n");
                ret = false;
                goto out;
        case CHECK_EVENT_RESULT_FAILED:
                ret = false;
                goto out;
        case CHECK_EVENT_RESULT_SUCCEEDED:
                goto out;
        }

        assert(!"Unexpected check_event result");

out:
        vsx_list_remove(&listener.listener.link);

        return ret;
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

static struct harness *
create_harness(void)
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

        harness->connection = vsx_connection_new("test_room",
                                                 "test_player");
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
                goto error;
        }

        if (!check_event(harness,
                         VSX_CONNECTION_EVENT_TYPE_RUNNING_STATE_CHANGED,
                         check_started_running_cb,
                         (const uint8_t *) "", 0,
                         NULL /* user_data */))
            goto error;

        return harness;

error:
        free_harness(harness);
        return NULL;
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

        return check_event(harness,
                           VSX_CONNECTION_EVENT_TYPE_HEADER,
                           check_header_cb,
                           player_id_header,
                           sizeof player_id_header - 1,
                           NULL /* user_data */);
}

static struct harness *
create_negotiated_harness(void)
{
        struct harness *harness = create_harness();

        if (harness == NULL)
                return NULL;

        if (!read_ws_request(harness))
                goto error;

        if (!write_data(harness, (const uint8_t *) "\r\n\r\n", 4))
                goto error;

        if (!read_new_player_request(harness))
                goto error;

        if (!send_player_id(harness))
                goto error;

        return harness;

error:
        free_harness(harness);
        return NULL;
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
check_tiles_cb(const struct vsx_game_state_tile *tile,
               void *user_data)
{
        struct check_tiles_closure *closure = user_data;
        int tile_num = tile->number;

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

        if (x != tile->x || y != tile->y) {
                fprintf(stderr,
                        "Wrong tile position reported.\n"
                        " Expected: %i,%i\n"
                        " Received: %i,%i\n",
                        x, y,
                        tile->x, tile->y);
                closure->succeeded = false;
                return;
        }

        char letter = tile_num % 26 + 'A';

        if (letter != tile->letter) {
                fprintf(stderr,
                        "Reported tile letter does not match. (%c != %c)\n",
                        letter,
                        tile->letter);
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

        if (vsx_game_state_get_n_tiles(harness->game_state) != 256) {
                fprintf(stderr,
                        "After sending all tiles the game state reported "
                        "%zu tiles\n",
                        vsx_game_state_get_n_tiles(harness->game_state));
                ret = false;
                goto out;
        }

out:
        free_harness(harness);

        return ret;
}

int
main(int argc, char **argv)
{
        int ret = EXIT_SUCCESS;

        if (!test_send_all_tiles())
                ret = EXIT_FAILURE;

        vsx_main_thread_clean_up();

        return ret;
}
