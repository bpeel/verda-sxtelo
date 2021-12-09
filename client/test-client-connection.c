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
#include "vsx-proto.h"

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

        const struct vsx_error_domain *expected_error_domain;
        int expected_error_code;
        const char *expected_error_message;
};

struct frame_error_test
{
        const uint8_t *frame;
        size_t frame_length;
        const char *expected_message;
};

enum check_event_result {
        CHECK_EVENT_RESULT_NO_MESSAGE,
        CHECK_EVENT_RESULT_FAILED,
        CHECK_EVENT_RESULT_SUCCEEDED,
};

typedef bool
(* check_event_func)(struct harness *harness,
                     const struct vsx_connection_event *event);

struct check_event_listener {
        struct vsx_listener listener;
        enum check_event_result result;
        enum vsx_connection_event_type expected_type;
        check_event_func cb;
        struct harness *harness;
};

#define BIN_STR(x) ((const uint8_t *) (x)), (sizeof (x)) - 1

static const struct frame_error_test
frame_error_tests[] = {
        {
                BIN_STR("\x82\x09\x00\x00ghijklm"),
                "The server sent an invalid player_id command"
        },
        {
                BIN_STR("\x82\x09\x01\x00ghijklm"),
                "The server sent an invalid message command"
        },
        {
                BIN_STR("\x82\x02\x03g"),
                "The server sent an invalid tile command"
        },
        {
                BIN_STR("\x82\x04\x04!\0?"),
                "The server sent an invalid player_name command"
        },
        {
                BIN_STR("\x82\x01\x05"),
                "The server sent an invalid player command"
        },
        {
                BIN_STR("\x82\x01\x06"),
                "The server sent an invalid player_shouted command"
        },
        {
                BIN_STR("\x82\x04\x08!!!"),
                "The server sent an invalid end command"
        },
        {
                BIN_STR("\x82\x00"),
                "The server sent an empty message"
        },
        {
                BIN_STR("\x82\x7e\x04\x01 This has a length of 1025 …"),
                "The server sent a frame that is too long"
        },
};

static void
handle_error(struct harness *harness,
             struct vsx_error *error)
{
        if (harness->expected_error_domain == NULL) {
                fprintf(stderr, "Unexpected error reported\n");
        }
        assert(harness->expected_error_domain);

        if (harness->expected_error_domain != error->domain) {
                fprintf(stderr,
                        "Error does not have the expected domain\n");
        }
        assert(harness->expected_error_domain == error->domain);

        if (harness->expected_error_code != error->code) {
                fprintf(stderr,
                        "Error does not have expected code (%i != %i)\n",
                        harness->expected_error_code,
                        error->code);
        }
        assert(harness->expected_error_code == error->code);

        if (strcmp(harness->expected_error_message,
                   error->message)) {
                fprintf(stderr,
                        "Error does not have expected message\n"
                        "Expected: %s\n"
                        "Received: %s\n",
                        harness->expected_error_message,
                        error->message);
        }
        assert(!strcmp(harness->expected_error_message, error->message));

        harness->expected_error_domain = NULL;
        harness->expected_error_code = 0;
        harness->expected_error_message = NULL;
}

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
                handle_error(harness, event->error.error);
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
write_string(struct harness *harness,
             const char *str)
{
        return write_data(harness, (const uint8_t *) str, strlen(str));
}

static bool
test_frame_error(struct harness *harness,
                 const struct frame_error_test *test)
{
        if (!write_string(harness, "\r\n\r\n"))
                return false;

        harness->expected_error_domain = &vsx_connection_error;
        harness->expected_error_code = VSX_CONNECTION_ERROR_BAD_DATA;
        harness->expected_error_message = test->expected_message;

        if (!write_data(harness, test->frame, test->frame_length))
                return false;

        if (harness->expected_error_domain != NULL) {
                fprintf(stderr,
                        "Expected error but non received\n"
                        " Expected: %s\n",
                        test->expected_message);
                return false;
        }

        return true;
}

static bool
test_frame_errors(void)
{
        bool ret = true;

        for (unsigned i = 0; i < VSX_N_ELEMENTS(frame_error_tests); i++) {
                struct harness *harness = create_harness();

                if (!test_frame_error(harness, frame_error_tests + i))
                        ret = false;

                free_harness(harness);
        }

        return ret;
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
        } else if (ce_listener->cb(ce_listener->harness, event)) {
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
            size_t data_len)
{
        struct check_event_listener listener = {
                .listener = { .notify = check_event_cb },
                .result = CHECK_EVENT_RESULT_NO_MESSAGE,
                .expected_type = expected_type,
                .cb = cb,
                .harness = harness,
        };

        vsx_signal_add(harness->event_signal, &listener.listener);

        bool write_ret = write_data(harness, data, data_len);

        vsx_list_remove(&listener.listener.link);

        if (!write_ret)
                return false;

        switch (listener.result) {
        case CHECK_EVENT_RESULT_NO_MESSAGE:
                fprintf(stderr, "No event received when one was expected\n");
                return false;
        case CHECK_EVENT_RESULT_FAILED:
                return false;
        case CHECK_EVENT_RESULT_SUCCEEDED:
                return true;
        }

        assert(!"Unexpected check_event result");

        return false;
}

static bool
check_state_in_progress_cb(struct harness *harness,
                           const struct vsx_connection_event *event)
{
        if (event->state_changed.state != VSX_CONNECTION_STATE_IN_PROGRESS) {
                fprintf(stderr,
                        "Expected state to be in-progress, but got %i\n",
                        event->state_changed.state);
                return false;
        }

        return true;
}

static bool
send_player_id(struct harness *harness)
{
        static const uint8_t header[] = "\x82\x0a\x00ghijklmn\x00";

        return check_event(harness,
                           VSX_CONNECTION_EVENT_TYPE_STATE_CHANGED,
                           check_state_in_progress_cb,
                           header,
                           sizeof header - 1);
}

static bool
check_player_changed_cb(struct harness *harness,
                        const struct vsx_connection_event *event)
{
        if (event->player_changed.player == NULL ||
            event->player_changed.player !=
            vsx_connection_get_self(harness->connection)) {
                fprintf(stderr,
                        "Changed player is not self\n");
                return false;
        }

        return true;
}

static bool
send_player_data(struct harness *harness)
{

        static const uint8_t name_header[] =
                /* player_name */
                "\x82\x0e\x04\x00test_player\x00";
        static const uint8_t data_header[] =
                /* player */
                "\x82\x03\x05\x00\x01";

        return (check_event(harness,
                            VSX_CONNECTION_EVENT_TYPE_PLAYER_CHANGED,
                            check_player_changed_cb,
                            name_header,
                            sizeof name_header - 1) &&
                check_event(harness,
                            VSX_CONNECTION_EVENT_TYPE_PLAYER_CHANGED,
                            check_player_changed_cb,
                            data_header,
                            sizeof data_header - 1));
}

static struct harness *
create_negotiated_harness(void)
{
        struct harness *harness = create_harness();

        if (harness == NULL)
                return NULL;

        if (!read_ws_request(harness))
                goto error;

        if (!write_string(harness, "\r\n\r\n"))
                goto error;

        if (!read_new_player_request(harness))
                goto error;

        if (!send_player_id(harness))
                goto error;

        if (!send_player_data(harness))
                goto error;

        const struct vsx_player *self =
                vsx_connection_get_self(harness->connection);
        const char *name = vsx_player_get_name(self);

        if (strcmp(name, "test_player")) {
                fprintf(stderr,
                        "self name does not match\n"
                        " Expected: test_player\n"
                        " Received: %s\n",
                        name);
                goto error;
        }

        if (!vsx_player_is_connected(self)) {
                fprintf(stderr, "self is not connected\n");
                goto error;
        }

        if (vsx_player_is_typing(self)) {
                fprintf(stderr, "self is typing after connecting\n");
                goto error;
        }

        if (vsx_player_has_next_turn(self)) {
                fprintf(stderr, "self has next turn after connecting\n");
                goto error;
        }

        if (vsx_player_get_number(self) != 0) {
                fprintf(stderr,
                        "self number is not 0 (%i)\n",
                        vsx_player_get_number(self));
                goto error;
        }

        return harness;

error:
        free_harness(harness);
        return NULL;
}

static bool
check_player_added_cb(struct harness *harness,
                      const struct vsx_connection_event *event)
{
        const struct vsx_player *other = event->player_changed.player;
        int number = vsx_player_get_number(other);

        if (number != 1) {
                fprintf(stderr,
                        "Expected other player to have number 1 but got %i\n",
                        number);
                return false;
        }

        const char *name = vsx_player_get_name(other);

        if (strcmp(name, "George")) {
                fprintf(stderr,
                        "Other player is not called George: %s\n",
                        name);
                return false;
        }

        return true;
}

static bool
add_player(struct harness *harness)
{
        static const uint8_t add_player_message[] =
                "\x82\x09\x04\x01George\x00";

        return check_event(harness,
                           VSX_CONNECTION_EVENT_TYPE_PLAYER_CHANGED,
                           check_player_added_cb,
                           add_player_message,
                           sizeof add_player_message - 1);
}

static bool
check_self_shouted_cb(struct harness *harness,
                      const struct vsx_connection_event *event)
{
        const struct vsx_player *self =
                vsx_connection_get_self (harness->connection);
        const struct vsx_player *shouter = event->player_shouted.player;

        if (self != shouter) {
                fprintf(stderr,
                        "Expected self to shout but got %i\n",
                        vsx_player_get_number(shouter));
                return false;
        }

        return true;
}

static bool
check_other_shouted_cb(struct harness *harness,
                       const struct vsx_connection_event *event)
{
        const struct vsx_player *shouter = event->player_shouted.player;
        int number = vsx_player_get_number(shouter);

        if (number != 1) {
                fprintf(stderr,
                        "Expected other to shout but got %i\n",
                        number);
                return false;
        }

        return true;
}

static bool
test_receive_shout(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        bool ret = true;

        static const uint8_t self_shout_message[] =
                "\x82\x02\x06\x00";

        if (!check_event(harness,
                         VSX_CONNECTION_EVENT_TYPE_PLAYER_SHOUTED,
                         check_self_shouted_cb,
                         self_shout_message,
                         sizeof self_shout_message - 1)) {
                ret = false;
                goto out;
        }

        if (!add_player(harness)) {
                ret = false;
                goto out;
        }

        static const uint8_t other_shout_message[] =
                "\x82\x02\x06\x01";

        if (!check_event(harness,
                         VSX_CONNECTION_EVENT_TYPE_PLAYER_SHOUTED,
                         check_other_shouted_cb,
                         other_shout_message,
                         sizeof other_shout_message - 1)) {
                ret = false;
                goto out;
        }

out:
        free_harness(harness);

        return ret;
}

static bool
test_send_leave(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        vsx_connection_leave(harness->connection);

        bool ret = (wake_up_connection(harness) &&
                    expect_data(harness, (uint8_t *) "\x82\x01\x84", 3));

        free_harness(harness);

        return ret;
}

static bool
test_send_shout(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        vsx_connection_shout(harness->connection);

        bool ret = (wake_up_connection(harness) &&
                    expect_data(harness, (uint8_t *) "\x82\x01\x8a", 3));

        free_harness(harness);

        return ret;
}

static bool
test_send_turn(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        vsx_connection_turn(harness->connection);

        bool ret = (wake_up_connection(harness) &&
                    expect_data(harness, (uint8_t *) "\x82\x01\x89", 3));

        free_harness(harness);

        return ret;
}

static bool
test_send_message(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        vsx_connection_send_message(harness->connection,
                                    "Eĥoŝanĝoĉiuĵaŭde "
                                    "c’est le mot des espérantistes");
        vsx_connection_send_message(harness->connection,
                                    "Du mesaĝoj?");

        static const uint8_t expected_response[] =
                "\x82\x3a\x85"
                "Eĥoŝanĝoĉiuĵaŭde "
                "c’est le mot des espérantistes\0"
                "\x82\x0e\x85"
                "Du mesaĝoj?\0";

        bool ret = true;
        const size_t buf_size = VSX_PROTO_MAX_MESSAGE_LENGTH + 16;
        char *buf = vsx_alloc(buf_size);

        if (!wake_up_connection(harness) ||
            !expect_data(harness,
                         expected_response,
                         sizeof expected_response - 1)) {
                ret = false;
                goto out;
        }

        /* Send a message that is too long. The vsx_connection should
         * clip it to a valid UTF-8 boundary.
         */
        memset(buf, 'a', VSX_PROTO_MAX_MESSAGE_LENGTH - 3);
        strcpy(buf + VSX_PROTO_MAX_MESSAGE_LENGTH - 3, "ĉĥ");

        vsx_connection_send_message(harness->connection, buf);

        memset(buf, 0, buf_size);

        const uint16_t payload_length =
                1 + (VSX_PROTO_MAX_MESSAGE_LENGTH - 1) + 1;

        buf[0] = 0x82;
        buf[1] = 0x7e; /* 16-bit payload length */
        buf[2] = payload_length >> 8;
        buf[3] = payload_length & 0xff;
        buf[4] = 0x85;
        memset(buf + 5, 'a', VSX_PROTO_MAX_MESSAGE_LENGTH - 3);
        strcpy(buf + 5 + VSX_PROTO_MAX_MESSAGE_LENGTH - 3, "ĉ");

        if (!wake_up_connection(harness) ||
            !expect_data(harness, (uint8_t *) buf, 4 + payload_length)) {
                ret = false;
                goto out;
        }

out:
        vsx_free(buf);
        free_harness(harness);

        return ret;
}

int
main(int argc, char **argv)
{
        int ret = EXIT_SUCCESS;

        if (!test_frame_errors())
                ret = EXIT_FAILURE;

        if (!test_slow_ws_response())
                ret = EXIT_FAILURE;

        if (!test_receive_shout())
                ret = EXIT_FAILURE;

        if (!test_send_leave())
                ret = EXIT_FAILURE;

        if (!test_send_shout())
                ret = EXIT_FAILURE;

        if (!test_send_turn())
                ret = EXIT_FAILURE;

        if (!test_send_message())
                ret = EXIT_FAILURE;

        return ret;
}
