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

#include "vsx-connection.h"
#include "vsx-worker.h"
#include "vsx-util.h"
#include "vsx-proto.h"

#define TEST_PORT 6133

struct harness {
        int server_sock;
        struct vsx_connection *connection;
        struct vsx_worker *worker;

        int server_fd;
};

static void
free_harness(struct harness *harness)
{
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
send_player_id(struct harness *harness)
{
        static const uint8_t player_id_header[] = "\x82\x0a\x00ghijklmn\x00";

        return write_data(harness,
                          player_id_header,
                          sizeof player_id_header - 1);
}

static bool
negotiate_connection(struct harness *harness)
{
        return (read_ws_request(harness) &&
                write_data(harness, (const uint8_t *) "\r\n\r\n", 4) &&
                read_new_player_request(harness) &&
                send_player_id(harness));
}

static bool
test_send_message(struct harness *harness)
{
        vsx_worker_lock(harness->worker);

        vsx_connection_send_message(harness->connection,
                                    "Hello, world!");

        vsx_worker_unlock(harness->worker);

        const uint8_t expected_data[] =
                "\x82\x0f\x85Hello, world!\0";

        return expect_data(harness,
                           expected_data,
                           sizeof expected_data - 1);
}

int
main(int argc, char **argv)
{
        struct harness *harness = create_harness();

        if (harness == NULL)
                return EXIT_FAILURE;

        int ret = EXIT_SUCCESS;

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
                ret = EXIT_FAILURE;
                goto out;
        }

        if (!negotiate_connection(harness) ||
            !test_send_message(harness)) {
                ret = EXIT_FAILURE;
                goto out;
        }

out:
        free_harness(harness);

        return ret;
}
