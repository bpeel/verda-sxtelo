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
#include <poll.h>
#include <unistd.h>

#include "vsx-connection.h"
#include "vsx-util.h"

#define TEST_PORT 6132

struct harness {
        int server_sock;
        struct vsx_connection *connection;
        struct vsx_listener event_listener;
        struct vsx_signal *event_signal;

        int poll_fd;
        short poll_events;
        int64_t wakeup_time;

        int server_fd;

        int events_triggered;

        struct vsx_error *error;
};

static void
event_cb(struct vsx_listener *listener,
         void *data)
{
        struct harness *harness = vsx_container_of(listener,
                                                   struct harness,
                                                   event_listener);
        const struct vsx_connection_event *event = data;

        harness->events_triggered |= 1 << event->type;

        switch (event->type) {
        case VSX_CONNECTION_EVENT_TYPE_ERROR:
                assert(harness->error == NULL);
                vsx_set_error(&harness->error,
                              event->error.error->domain,
                              event->error.error->code,
                              "%s",
                              event->error.error->message);
                break;

        case VSX_CONNECTION_EVENT_TYPE_POLL_CHANGED:
                harness->poll_fd = event->poll_changed.fd;
                harness->poll_events = event->poll_changed.events;
                harness->wakeup_time = event->poll_changed.wakeup_time;
                break;

        default:
                break;
        }
}

static bool
wake_up_connection(struct harness *harness)
{
        struct pollfd fd = {
                .fd = harness->poll_fd,
                .events = harness->poll_events,
                .revents = 0,
        };

        if (poll(&fd,
                 harness->poll_fd == -1 ? 0 : 1 /* nfds */,
                 0 /* timeout */) == -1) {
                fprintf(stderr, "poll failed: %s\n", strerror(errno));
                return false;
        }

        vsx_connection_wake_up(harness->connection, fd.revents);

        return true;
}

static void
free_harness(struct harness *harness)
{
        if (harness->server_fd != -1)
                vsx_close(harness->server_fd);

        if (harness->server_sock != -1)
                vsx_close(harness->server_sock);

        if (harness->connection)
                vsx_connection_free(harness->connection);

        if (harness->error)
                vsx_error_free(harness->error);

        vsx_free(harness);
}

static struct harness *
create_harness(void)
{
        struct harness *harness = vsx_calloc(sizeof *harness);

        harness->server_fd = -1;

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

        harness->connection = vsx_connection_new(&local_address,
                                                 "test_room",
                                                 "test_player");

        harness->event_signal =
                vsx_connection_get_event_signal(harness->connection);

        harness->event_listener.notify = event_cb;
        vsx_signal_add(harness->event_signal, &harness->event_listener);

        vsx_connection_set_running(harness->connection, true);

        if (!wake_up_connection(harness))
                goto error;

        if (harness->poll_fd == -1) {
                fprintf(stderr,
                        "After starting the connection, there is no poll fd\n");
                goto error;
        }

        if (!wake_up_connection(harness))
                goto error;

        harness->server_fd = accept(harness->server_sock,
                                    NULL, /* addr */
                                    NULL /* addrlen */);

        if (harness->server_fd == -1) {
                fprintf(stderr,
                        "accept failed: %s\n",
                        strerror(errno));
                goto error;
        }

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
        if (!wake_up_connection(harness))
                return false;

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

        return wake_up_connection(harness);
}

static bool
test_slow_ws_response(void)
{
        struct harness *harness = create_harness();

        if (harness == NULL)
                return false;

        bool ret = true;

        if (!read_ws_request(harness)) {
                ret = false;
                goto out;
        }

        static const uint8_t ws_response[] =
                /* The connection is just searching for “\r\n\r\n”.
                 * This tries to send every substring of this before
                 * sending the final full terminator.
                 */
                "\r nope"
                "\r\n nope"
                "\r\n\r nope"
                "\r\n\r\n";

        for (const uint8_t *p = ws_response; *p; p++) {
                if (!write_data(harness, p, 1)) {
                        ret = false;
                        goto out;
                }
        }

        if (!read_new_player_request(harness)) {
                ret = false;
                goto out;
        }

        harness->events_triggered = 0;

        /* Send the player id and n_tiles response so we can check
         * that it sucessfully switched to parsing frames.
         */
        static const uint8_t commands[] =
                "\x82\x0a\x00ghijklmn\x0"
                "\x82\x0e\x04\x00test_player\x0";
        if (!write_data(harness, commands, (sizeof commands) - 1)) {
                ret = false;
                goto out;
        }

        if ((harness->events_triggered &
             (1 << VSX_CONNECTION_EVENT_TYPE_PLAYER_CHANGED)) == 0) {
                fprintf(stderr,
                        "Connection didn’t send player_changed event after "
                        "receiving command\n");
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

        if (!test_slow_ws_response())
                ret = EXIT_FAILURE;

        return ret;
}
