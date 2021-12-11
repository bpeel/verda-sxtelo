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
#include "vsx-monotonic.h"
#include "vsx-file-error.h"
#include "vsx-buffer.h"

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
        {
                BIN_STR("\x82\x7f\x00\x01\x00\x00 This has a length of "
                        "65536 …"),
                "The server sent a frame that is too long"
        },
};

/* Hack to replace vsx_monotonic_get for the tests so we can fake the
 * passage of time and still make the test run instantly.
 */
#define vsx_monotonic_get original_vsx_monotonic_get
#undef VSX_MONOTONIC_H
#include "vsx-monotonic.c"
#undef vsx_monotonic_get

static bool replace_monotonic_time = false;
static int64_t replacement_monotonic_time = 0;

int64_t
vsx_monotonic_get(void)
{
        if (replace_monotonic_time)
                return replacement_monotonic_time;
        else
                return original_vsx_monotonic_get();
}

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

static bool
fd_ready_for_read(int fd)
{
        struct pollfd pfd = {
                .fd = fd,
                .events = POLLIN,
                .revents = 0,
        };

        if (poll(&pfd, 1 /* nfds */, 0 /* timeout */) == -1) {
                fprintf(stderr,
                        "poll failed: %s\n",
                        strerror(errno));
                assert(!"poll failed");
        }

        return pfd.revents;
}

static bool
accept_connection(struct harness *harness)
{
        if (!fd_ready_for_read(harness->server_sock)) {
                fprintf(stderr,
                        "The test wants to accept a connection but the "
                        "server socket is not ready for reading.\n");
                return false;
        }

        harness->server_fd = accept(harness->server_sock,
                                    NULL, /* addr */
                                    NULL /* addrlen */);

        if (harness->server_fd == -1) {
                fprintf(stderr,
                        "accept failed: %s\n",
                        strerror(errno));
                return false;
        }

        return true;
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

        if (!accept_connection(harness))
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
                           const struct vsx_connection_event *event,
                           void *user_data)
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
                           sizeof header - 1,
                           NULL /* user_data */);
}

static bool
check_player_changed_cb(struct harness *harness,
                        const struct vsx_connection_event *event,
                        void *user_data)
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
                            sizeof name_header - 1,
                            NULL /* user_data */) &&
                check_event(harness,
                            VSX_CONNECTION_EVENT_TYPE_PLAYER_CHANGED,
                            check_player_changed_cb,
                            data_header,
                            sizeof data_header - 1,
                            NULL /* user_data */));
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
do_unexpected_close(struct harness *harness)
{
        /* Close the server end of the socket so that the client will
         * need to reconnect.
         */
        vsx_close(harness->server_fd);
        harness->server_fd = -1;

        harness->expected_error_domain = &vsx_connection_error;
        harness->expected_error_code = VSX_CONNECTION_ERROR_CONNECTION_CLOSED;
        harness->expected_error_message =
                "The server unexpectedly closed the connection";

        if (!wake_up_connection(harness))
                return false;

        if (harness->expected_error_domain) {
                fprintf(stderr,
                        "The connection didn’t report an error after the "
                        "server socket was closed\n");
                return false;
        }

        return true;
}

static struct harness *
prepare_reconnect_test(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return NULL;

        /* Send a few messages so we verify that the connection sends
         * the message num in the reconnect message.
         */
        static const uint8_t messages[] =
                "\x82\x05\x01ghi\0"
                "\x82\x05\x01jkl\0";

        if (!write_data(harness, messages, sizeof messages - 1))
                goto error;

        if (!do_unexpected_close(harness))
                goto error;

        /* The first reconnect should be immediate */
        if (harness->wakeup_time > vsx_monotonic_get()) {
                fprintf(stderr,
                        "The connection isn’t ready to be woken up immediately "
                        "after recognising the connection has closed.\n");
                goto error;
        }

        return harness;

error:
        free_harness(harness);
        return NULL;
}

static bool
read_reconnect_message(struct harness *harness)
{
        static const uint8_t reconnect_message[] =
                "\x82\x0b\x81ghijklmn\x02\x00";

        return expect_data(harness,
                           reconnect_message,
                           sizeof reconnect_message - 1);
}

static bool
test_immediate_reconnect(void)
{
        struct harness *harness = prepare_reconnect_test();
        bool ret = true;

        if (harness == NULL)
                return false;

        if (!wake_up_connection(harness)) {
                ret = false;
                goto out;
        }

        if (!accept_connection(harness)) {
                ret = false;
                goto out;
        }

        if (!read_ws_request(harness) ||
            !write_string(harness, "\r\n\r\n")) {
                ret = false;
                goto out;
        }

        if (!read_reconnect_message(harness)) {
                ret = false;
                goto out;
        }

out:
        free_harness(harness);

        return ret;
}

static bool
test_reconnect_delay(void)
{
        struct harness *harness = prepare_reconnect_test();
        bool ret = true;

        if (harness == NULL)
                return false;

        replacement_monotonic_time = vsx_monotonic_get();
        replace_monotonic_time = true;

        int64_t delay = 16000000;

        for (int i = 0; i < 3; i++) {
                if (!wake_up_connection(harness)) {
                        ret = false;
                        goto out;
                }

                if (!accept_connection(harness)) {
                        ret = false;
                        goto out;
                }

                if (!read_ws_request(harness)) {
                        ret = false;
                        goto out;
                }

                if (!read_reconnect_message(harness)) {
                        ret = false;
                        goto out;
                }

                if (!do_unexpected_close(harness)) {
                        ret = false;
                        goto out;
                }

                if (harness->wakeup_time <
                    vsx_monotonic_get() + delay - 1000000) {
                        fprintf(stderr,
                                "Expected connection to delay for at least "
                                "%f seconds but only %f are requested\n",
                                delay / 1000000.0,
                                (harness->wakeup_time - vsx_monotonic_get()) /
                                1000000.0);
                        ret = false;
                        goto out;
                }

                /* Advance time to 1 second before the delay */
                replacement_monotonic_time += delay - 1000000;

                if (!wake_up_connection(harness)) {
                        ret = false;
                        goto out;
                }

                /* Make sure the connection didn’t try to connect */
                if (fd_ready_for_read(harness->server_sock)) {
                        fprintf(stderr,
                                "Connection tried to connect before timeout is "
                                "up\n");
                        ret = false;
                        goto out;
                }

                /* Advance enough time to trigger the reconnect */
                replacement_monotonic_time += 1000001;

                delay *= 2;
        }

out:
        replace_monotonic_time = false;
        free_harness(harness);

        return ret;
}

static bool
test_keep_alive(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        bool ret = true;

        replacement_monotonic_time = vsx_monotonic_get();
        replace_monotonic_time = true;

        /* The next wakeup time should be at least 2.5 minutes in the future */
        if (harness->wakeup_time == INT64_MAX ||
            harness->wakeup_time <
            replacement_monotonic_time + (2 * 60 + 30 - 1) * 1000000) {
                fprintf(stderr,
                        "Next wakeup time for newly negotiated connection "
                        "should be at least 2.5 minutes in the future but it "
                        "is %f seconds\n",
                        (harness->wakeup_time - replacement_monotonic_time) /
                        1000000.0);
                ret = false;
                goto out;
        }

        /* Advance time to nearly enough */
        replacement_monotonic_time += (2 * 60 + 30 - 1) * 1000000;

        if (!wake_up_connection(harness)) {
                ret = false;
                goto out;
        }

        /* Check that nothing was written */
        if (fd_ready_for_read(harness->server_fd)) {
                fprintf(stderr,
                        "The vsx_connection wrote something before the "
                        "keep up delay.\n");
                ret = false;
                goto out;
        }

        /* Now advance actually enough time */
        replacement_monotonic_time += 1000001;

        if (!wake_up_connection(harness)) {
                ret = false;
                goto out;
        }

        if (!expect_data(harness, (const uint8_t *) "\x82\x01\x83", 3)) {
                ret = false;
                goto out;
        }

out:
        replace_monotonic_time = false;
        free_harness(harness);

        return ret;
}

static bool
check_player_added_cb(struct harness *harness,
                      const struct vsx_connection_event *event,
                      void *user_data)
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
                           sizeof add_player_message - 1,
                           NULL /* user_data */);
}

static bool
check_self_shouted_cb(struct harness *harness,
                      const struct vsx_connection_event *event,
                      void *user_data)
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
                       const struct vsx_connection_event *event,
                       void *user_data)
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
                         sizeof self_shout_message - 1,
                         NULL /* user_data */)) {
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
                         sizeof other_shout_message - 1,
                         NULL /* user_data */)) {
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

static bool
test_typing(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        bool ret = true;

        vsx_connection_set_typing(harness->connection, true);

        if (!vsx_connection_get_typing(harness->connection)) {
                fprintf(stderr,
                        "Typing not true after setting it to true\n");
                ret = false;
                goto out;
        }

        static const uint8_t typing_message[] =
                "\x82\x01\x86";

        if (!expect_data(harness, typing_message, sizeof typing_message - 1)) {
                ret = false;
                goto out;
        }

        /* Setting it to the same value shouldn’t do anything */
        vsx_connection_set_typing(harness->connection, true);

        if (!wake_up_connection(harness)) {
                ret = false;
                goto out;
        }

        if (fd_ready_for_read(harness->server_fd)) {
                fprintf(stderr,
                        "Connection wrote something after setting typing "
                        "to same value\n");
                ret = false;
                goto out;
        }

        vsx_connection_set_typing(harness->connection, false);

        static const uint8_t untyping_message[] =
                "\x82\x01\x87";

        if (!expect_data(harness,
                         untyping_message,
                         sizeof untyping_message - 1)) {
                ret = false;
                goto out;
        }

        vsx_connection_set_typing(harness->connection, true);

        if (!expect_data(harness, typing_message, sizeof typing_message - 1)) {
                ret = false;
                goto out;
        }

        vsx_connection_send_message(harness->connection, "hi");

        vsx_connection_set_typing(harness->connection, false);

        if (!expect_data(harness, (uint8_t *) "\x82\x04\x85hi\0", 6)) {
                ret = false;
                goto out;
        }

        /* Sending a message should automatically set the typing state
         * to false so the client shouldn’t send another message.
         */
        if (!wake_up_connection(harness)) {
                ret = false;
                goto out;
        }

        if (fd_ready_for_read(harness->server_fd)) {
                fprintf(stderr,
                        "Connection is trying to write something after "
                        "sending a message and setting typing to false\n");
                ret = false;
                goto out;
        }

out:
        free_harness(harness);

        return ret;
}

struct send_tile_closure {
        int num;
        int x;
        int y;
        char letter;
        bool is_new;
};

static bool
check_tile_changed_cb(struct harness *harness,
                      const struct vsx_connection_event *event,
                      void *user_data)
{
        struct send_tile_closure *closure = user_data;

        const struct vsx_tile *tile = event->tile_changed.tile;

        if (vsx_tile_get_number(tile) != closure->num ||
            vsx_tile_get_x(tile) != closure->x ||
            vsx_tile_get_y(tile) != closure->y ||
            vsx_tile_get_letter(tile) != closure->letter ||
            event->tile_changed.new_tile != closure->is_new) {
                fprintf(stderr,
                        "Tile from event does not match sent tile:\n"
                        " Expected: %i %i,%i %c %s\n"
                        " Received: %i %i,%i %c %s\n",
                        closure->num,
                        closure->x,
                        closure->y,
                        closure->letter,
                        closure->is_new ? "new" : "old",
                        vsx_tile_get_number(tile),
                        vsx_tile_get_x(tile),
                        vsx_tile_get_y(tile),
                        vsx_tile_get_letter(tile),
                        event->tile_changed.new_tile ? "new" : "old");
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
          uint8_t player,
          bool is_new)
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
                .is_new = is_new,
        };

        return check_event(harness,
                           VSX_CONNECTION_EVENT_TYPE_TILE_CHANGED,
                           check_tile_changed_cb,
                           add_tile_message,
                           sizeof add_tile_message - 1,
                           &closure);
}

static bool
test_move_tile(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        bool ret = true;

        /* Add three tiles to the game */
        for (int i = 0; i < 3; i++) {
                if (!send_tile(harness,
                               i,
                               i * 2,
                               i * 2 + 1,
                               'a' + i,
                               i /* player */,
                               true /* is_new */)) {
                        ret = false;
                        goto out;
                }
        }

        /* Move four tiles */
        for (int i = 0; i < 4; i++) {
                vsx_connection_move_tile(harness->connection,
                                         i,
                                         i * 2 + 5,
                                         i * 2 + 1);
        }

        /* Move one of the tiles again */
        vsx_connection_move_tile(harness->connection, 0, 3, 5);

        /* We should only get 4 move commands because the second move
         * of the same tile should be squashed into one.
         */
        const uint8_t expected_data[] =
                "\x82\x06\x88\x00\x03\x00\x05\x00"
                "\x82\x06\x88\x01\x07\x00\x03\x00"
                "\x82\x06\x88\x02\x09\x00\x05\x00"
                "\x82\x06\x88\x03\x0b\x00\x07\x00";

        if (!expect_data(harness, expected_data, sizeof expected_data - 1)) {
                ret = false;
                goto out;
        }

        if (fd_ready_for_read(harness->server_fd)) {
                fprintf(stderr,
                        "Connection sent more data after typing commands\n");
                ret = false;
                goto out;
        }

out:
        free_harness(harness);

        return ret;
}

struct check_tiles_closure {
        struct harness *harness;
        int next_tile_num;
        bool succeeded;
};

static void
check_tiles_cb(const struct vsx_tile *tile,
               void *user_data)
{
        struct check_tiles_closure *closure = user_data;
        int tile_num = closure->next_tile_num++;

        if (tile_num != vsx_tile_get_number(tile)) {
                fprintf(stderr,
                        "Tiles reported out of order. Expected %i got %i\n",
                        tile_num,
                        vsx_tile_get_number(tile));
                closure->succeeded = false;
                return;
        }

        int16_t x = tile_num * 257;
        int y = (tile_num & 1) ? -tile_num : tile_num;

        if (x != vsx_tile_get_x(tile) ||
            y != vsx_tile_get_y(tile)) {
                fprintf(stderr,
                        "Wrong tile position reported.\n"
                        " Expected: %i,%i\n"
                        " Received: %i,%i\n",
                        x, y,
                        vsx_tile_get_x(tile),
                        vsx_tile_get_y(tile));
                closure->succeeded = false;
                return;
        }

        char letter = tile_num % 26 + 'A';

        if (letter != vsx_tile_get_letter(tile)) {
                fprintf(stderr,
                        "Reported tile letter does not match. (%c != %c)\n",
                        letter,
                        vsx_tile_get_letter(tile));
                closure->succeeded = false;
                return;
        }

        if (vsx_connection_get_tile(closure->harness->connection,
                                    tile_num) != tile) {
                fprintf(stderr,
                        "Tile reported by get_tile not same as iterating "
                        "tiles\n");
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
                               tile_num / 2,
                               true /* is_new */)) {
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
                       0,
                       false /* is_new */)) {
                ret = false;
                goto out;
        }

        struct check_tiles_closure closure = {
                .harness = harness,
                .next_tile_num = 0,
                .succeeded = true,
        };

        vsx_connection_foreach_tile(harness->connection,
                                    check_tiles_cb,
                                    &closure);

        if (!closure.succeeded) {
                ret = false;
                goto out;
        }

        if (closure.next_tile_num != 256) {
                fprintf(stderr,
                        "vsx_connection_foreach_tile didn’t report "
                        "all the tiles\n");
                ret = false;
                goto out;
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
check_players_cb(const struct vsx_player *player,
                 void *user_data)
{
        struct check_players_closure *closure = user_data;
        int player_num = closure->next_player_num++;

        if (player_num != vsx_player_get_number(player)) {
                fprintf(stderr,
                        "Players reported out of order. Expected %i got %i\n",
                        player_num,
                        vsx_player_get_number(player));
                closure->succeeded = false;
                return;
        }

        struct vsx_buffer buf = VSX_BUFFER_STATIC_INIT;

        if (player_num == 1)
                vsx_buffer_append_string(&buf, "George");
        else
                vsx_buffer_append_printf(&buf, "Player %i", player_num);

        if (strcmp(vsx_player_get_name(player), (const char *) buf.data)) {
                fprintf(stderr,
                        "Wrong player name reported.\n"
                        " Expected: %s\n"
                        " Received: %s\n",
                        (const char *) buf.data,
                        vsx_player_get_name(player));
                closure->succeeded = false;
        }

        vsx_buffer_destroy(&buf);

        if (vsx_connection_get_player(closure->harness->connection,
                                      player_num) != player) {
                fprintf(stderr,
                        "Player reported by get_player not same as iterating "
                        "players\n");
                closure->succeeded = false;
                return;
        }
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
                int player_num = ((i & 0xfc) |
                                  ((i & 2) >> 1) |
                                  ((i & 1) << 1));

                vsx_buffer_set_length(&buf, 0);
                vsx_buffer_append_string(&buf, "\x82\xff\x04\xff");
                vsx_buffer_append_printf(&buf, "Player %i", player_num);
                buf.length++;
                buf.data[1] = buf.length - 2;
                buf.data[3] = player_num;

                if (!write_data(harness, buf.data, buf.length)) {
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

        vsx_connection_foreach_player(harness->connection,
                                      check_players_cb,
                                      &closure);

        if (!closure.succeeded) {
                ret = false;
                goto out;
        }

        if (closure.next_player_num != 256) {
                fprintf(stderr,
                        "vsx_connection_foreach_player didn’t report "
                        "all the players\n");
                ret = false;
                goto out;
        }

out:
        vsx_buffer_destroy(&buf);
        free_harness(harness);

        return ret;
}

static bool
check_end_state_cb(struct harness *harness,
                   const struct vsx_connection_event *event,
                   void *user_data)
{
        if (event->state_changed.state != VSX_CONNECTION_STATE_DONE) {
                fprintf(stderr,
                        "State is not DONE after sending END\n");
                return false;
        }

        if (event->state_changed.state !=
            vsx_connection_get_state(harness->connection)) {
                fprintf(stderr,
                        "State in event does not match connection state\n");
                return false;
        }

        return true;
}

static bool
test_end(bool do_shutdown)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        bool ret = true;

        if (!check_event(harness,
                         VSX_CONNECTION_EVENT_TYPE_STATE_CHANGED,
                         check_end_state_cb,
                         (const uint8_t *) "\x82\x01\x08", 3,
                         NULL /* user_data */)) {
                ret = false;
                goto out;
        }

        /* If do_shutdown is false the vsx_connection should initiate
         * the graceful shutdown itself when it no longer has anything
         * to write.
         */
        if (do_shutdown) {
                /* Initiate a graceful shutdown */
                shutdown(harness->server_fd, SHUT_WR);

                if (!wake_up_connection(harness)) {
                        ret = false;
                        goto out;
                }
        }

        if (!fd_ready_for_read(harness->server_fd)) {
                fprintf(stderr,
                        "Socket not ready for reading after initiating "
                        "graceful shutdown\n");
                ret = false;
                goto out;
        }

        uint8_t byte;

        /* Reading should report EOF */
        int got = read(harness->server_fd, &byte, 1);

        if (got != 0) {
                fprintf(stderr,
                        "Expected EOF but read returned %i\n",
                        got);
                ret = false;
                goto out;
        }

        if (do_shutdown) {
                if (vsx_connection_get_running(harness->connection)) {
                        fprintf(stderr, "Connection still running after END\n");
                        ret = false;
                        goto out;
                }

                if (harness->poll_fd != -1) {
                        fprintf(stderr,
                                "Connection is still polling after END\n");
                        ret = false;
                        goto out;
                }
        } else {
                if (harness->poll_fd == -1 ||
                    (harness->poll_events & POLLIN) == 0) {
                        fprintf(stderr,
                                "Connection is not waiting for shutdown\n");
                        ret = false;
                        goto out;
                }
        }

out:
        free_harness(harness);

        return ret;
}

static bool
test_read_error(void)
{
        struct harness *harness = create_harness();

        bool ret = true;

        /* Let the connection add the data for the WS request */
        if (!wake_up_connection(harness)) {
                ret = false;
                goto out;
        }

        /* Close the connection without reading the data. This will
         * make the client receive an error rather than EOF.
         */
        vsx_close(harness->server_fd);
        harness->server_fd = -1;

        harness->expected_error_domain = &vsx_file_error;
        harness->expected_error_code = VSX_FILE_ERROR_OTHER;
        harness->expected_error_message =
                "Error reading from socket: Connection reset by peer";

        if (!wake_up_connection(harness)) {
                ret = false;
                goto out;
        }

        if (harness->expected_error_domain) {
                fprintf(stderr, "Expected read error but none received\n");
                ret = false;
                goto out;
        }

out:
        free_harness(harness);
        return ret;
}

static bool
test_write_buffer_full(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        bool ret = true;
        const int message_size = 1000;

        char *message = vsx_alloc(message_size + 1);
        memset(message, 'a', message_size);
        message[message_size] = '\0';

        /* Queue enough messages that it can’t be sent in a single write */
        vsx_connection_send_message(harness->connection, message);
        vsx_connection_send_message(harness->connection, message);

        int frame_length = message_size + 2;
        int total_size = frame_length + 4;

        char *frame = vsx_alloc(total_size);
        frame[0] = 0x82;
        frame[1] = 0x7e;
        frame[2] = frame_length >> 8;
        frame[3] = frame_length & 0xff;
        frame[4] = 0x85;
        strcpy(frame + 5, message);

        if (!expect_data(harness, (uint8_t *) frame, total_size)) {
                ret = false;
                goto out;
        }

        /* The connection shouln’t have written all of its pending data */
        if (fd_ready_for_read(harness->server_fd)) {
                fprintf(stderr,
                        "The connection more data than should fit in its "
                        "output buffer.\n");
                ret = false;
                goto out;
        }

        /* The frame for the second message should be there after
         * letting it write again.
         */
        if (!expect_data(harness, (uint8_t *) frame, total_size)) {
                ret = false;
                goto out;
        }

out:
        vsx_free(message);
        vsx_free(frame);
        free_harness(harness);

        return ret;
}

static bool
test_leak_pendings(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        /* Queue some messages and tiles to move */
        vsx_connection_send_message(harness->connection, "hi!");
        vsx_connection_move_tile(harness->connection, 0, 1, 2);

        /* Free the connection before it gets a chance to send them */
        free_harness(harness);
        return true;
}

int
main(int argc, char **argv)
{
        int ret = EXIT_SUCCESS;

        if (!test_frame_errors())
                ret = EXIT_FAILURE;

        if (!test_slow_ws_response())
                ret = EXIT_FAILURE;

        if (!test_immediate_reconnect())
                ret = EXIT_FAILURE;

        if (!test_reconnect_delay())
                ret = EXIT_FAILURE;

        if (!test_keep_alive())
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

        if (!test_typing())
                ret = EXIT_FAILURE;

        if (!test_move_tile())
                ret = EXIT_FAILURE;

        if (!test_send_all_tiles())
                ret = EXIT_FAILURE;

        if (!test_send_all_players())
                ret = EXIT_FAILURE;

        if (!test_end(true /* do_shutdown */))
                ret = EXIT_FAILURE;

        if (!test_end(false /* do_shutdown */))
                ret = EXIT_FAILURE;

        if (!test_read_error())
                ret = EXIT_FAILURE;

        if (!test_write_buffer_full())
                ret = EXIT_FAILURE;

        if (!test_leak_pendings())
                ret = EXIT_FAILURE;

        return ret;
}
