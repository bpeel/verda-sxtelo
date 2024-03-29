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
#include <inttypes.h>

#include "vsx-connection.h"
#include "vsx-util.h"
#include "vsx-proto.h"
#include "vsx-monotonic.h"
#include "vsx-file-error.h"
#include "vsx-buffer.h"

#define TEST_PORT 6132

struct harness {
        int server_sock;
        struct vsx_netaddress local_address;
        struct vsx_connection *connection;
        struct vsx_listener event_listener;
        struct vsx_signal *event_signal;

        /* All of the events get copied into this list and then
         * destroyed when the harness is destroyed in order to
         * test the event copying mechanism
         */
        struct vsx_list copied_events;

        int poll_fd;
        short poll_events;
        int64_t wakeup_time;

        int server_fd;

        int events_triggered;

        const struct vsx_error_domain *expected_error_domain;
        int expected_error_code;
        const char *expected_error_message;
};

struct copied_event {
        struct vsx_list link;
        struct vsx_connection_event event;
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
        int ignore_event_type;
        check_event_func cb;
        struct harness *harness;
        void *user_data;
};

#define BIN_STR(x) ((const uint8_t *) (x)), (sizeof (x)) - 1

static const uint8_t player_id_header[] = "\x82\x0a\x00ghijklmn\x00";
static const uint8_t conversation_id_header[] =
        "\x82\x09\x0a\x80\x81\x82\x83\x84\x85\x86\x87";

static const struct frame_error_test
frame_error_tests[] = {
        {
                BIN_STR("\x82\x09\x00\x00ghijklm"),
                "The server sent an invalid player_id command"
        },
        {
                BIN_STR("\x82\x08\x0aghijklm"),
                "The server sent an invalid conversation_id command"
        },
        {
                BIN_STR("\x82\x04\x02six"),
                "The server sent an invalid n_tiles command"
        },
        {
                BIN_STR("\x82\x04\x0cĉĉ"),
                "The server sent an invalid language command"
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
                BIN_STR("\x82\x04\x09!!!"),
                "The server sent an invalid bad player ID command"
        },
        {
                BIN_STR("\x82\x04\x0b!!!"),
                "The server sent an invalid bad conversation ID command"
        },
        {
                BIN_STR("\x82\x04\x0d!!!"),
                "The server sent an invalid conversation full command"
        },
        {
                BIN_STR("\x82\x04\x07!!!"),
                "The server sent an invalid sync command"
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

        struct copied_event *copied_event = vsx_calloc(sizeof *copied_event);
        vsx_connection_copy_event(&copied_event->event, event);
        vsx_list_insert(harness->copied_events.prev, &copied_event->link);

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
free_copied_events(struct harness *harness)
{
        struct copied_event *copied_event, *tmp;

        vsx_list_for_each_safe(copied_event,
                               tmp,
                               &harness->copied_events,
                               link) {
                vsx_connection_destroy_event(&copied_event->event);
                vsx_free(copied_event);
        }
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

        free_copied_events(harness);

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
create_harness_no_start(void)
{
        struct harness *harness = vsx_calloc(sizeof *harness);

        harness->poll_fd = -1;
        harness->wakeup_time = INT64_MAX;

        vsx_list_init(&harness->copied_events);

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

        if (!vsx_netaddress_from_string(&harness->local_address,
                                        "127.0.0.1",
                                        TEST_PORT)) {
                fprintf(stderr, "error getting localhost address\n");
                goto error;
        }

        struct vsx_netaddress_native native_local_address;

        vsx_netaddress_to_native(&harness->local_address,
                                 &native_local_address);

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

        harness->event_signal =
                vsx_connection_get_event_signal(harness->connection);

        harness->event_listener.notify = event_cb;
        vsx_signal_add(harness->event_signal, &harness->event_listener);

        return harness;

error:
        free_harness(harness);
        return NULL;
}

static bool
wake_up_and_accept_connection(struct harness *harness)
{
        if (!wake_up_connection(harness))
                return false;

        if (harness->poll_fd == -1) {
                fprintf(stderr,
                        "After starting the connection, there is no poll fd\n");
                return false;
        }

        if (!wake_up_connection(harness))
                return false;

        if (!accept_connection(harness))
                return false;

        return true;
}

static bool
start_connection(struct harness *harness)
{
        vsx_connection_set_room(harness->connection, "test_room");
        vsx_connection_set_player_name(harness->connection, "test_player");
        vsx_connection_set_address(harness->connection,
                                   &harness->local_address);

        vsx_connection_set_running(harness->connection, true);

        return wake_up_and_accept_connection(harness);
}

static struct harness *
create_harness(void)
{
        struct harness *harness = create_harness_no_start();

        if (harness == NULL)
                return NULL;

        if (!start_connection(harness)) {
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
             (1 << VSX_CONNECTION_EVENT_TYPE_PLAYER_NAME_CHANGED)) == 0) {
                fprintf(stderr,
                        "Connection didn’t send player_name_changed event "
                        "after receiving command\n");
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

        if (event->type == ce_listener->ignore_event_type)
                return;

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
check_event_with_ignore(struct harness *harness,
                        enum vsx_connection_event_type expected_type,
                        int ignore_event_type,
                        check_event_func cb,
                        const uint8_t *data,
                        size_t data_len,
                        void *user_data)
{
        struct check_event_listener listener = {
                .listener = { .notify = check_event_cb },
                .result = CHECK_EVENT_RESULT_NO_MESSAGE,
                .expected_type = expected_type,
                .ignore_event_type = ignore_event_type,
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
check_event(struct harness *harness,
            enum vsx_connection_event_type expected_type,
            check_event_func cb,
            const uint8_t *data,
            size_t data_len,
            void *user_data)
{
        return check_event_with_ignore(harness,
                                       expected_type,
                                       -1, /* ignore_event_type */
                                       cb,
                                       data,
                                       data_len,
                                       user_data);
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
        return check_event(harness,
                           VSX_CONNECTION_EVENT_TYPE_HEADER,
                           check_header_cb,
                           player_id_header,
                           sizeof player_id_header - 1,
                           NULL /* user_data */);
}

static bool
check_conversation_id_cb(struct harness *harness,
                         const struct vsx_connection_event *event,
                         void *user_data)
{
        const uint64_t expected_id = UINT64_C(0x8786858483828180);

        if (event->conversation_id.id != expected_id) {
                fprintf(stderr,
                        "conversation_id does not match in event\n"
                        " Expected: 0x%" PRIx64 "\n"
                        " Received: 0x%" PRIx64 "\n",
                        expected_id,
                        event->conversation_id.id);
                return false;
        }

        return true;
}

static bool
send_conversation_id(struct harness *harness)
{
        return check_event(harness,
                           VSX_CONNECTION_EVENT_TYPE_CONVERSATION_ID,
                           check_conversation_id_cb,
                           conversation_id_header,
                           sizeof conversation_id_header - 1,
                           NULL /* user_data */);
}

static bool
send_player_id_no_event(struct harness *harness)
{
        return write_data(harness,
                          player_id_header,
                          sizeof player_id_header - 1);
}

static bool
check_player_name_changed_cb(struct harness *harness,
                             const struct vsx_connection_event *event,
                             void *user_data)
{
        if (event->player_name_changed.player_num != 0) {
                fprintf(stderr,
                        "Changed player is not self\n");
                return false;
        }

        if (strcmp(event->player_name_changed.name, "test_player")) {
                fprintf(stderr,
                        "self name does not match\n"
                        " Expected: test_player\n"
                        " Received: %s\n",
                        event->player_name_changed.name);
                return false;
        }

        return true;
}

static bool
check_player_flags_changed_cb(struct harness *harness,
                              const struct vsx_connection_event *event,
                              void *user_data)
{
        if (event->player_flags_changed.player_num != 0) {
                fprintf(stderr,
                        "Changed player is not self\n");
                return false;
        }

        if (event->player_flags_changed.flags != 1) {
                fprintf(stderr,
                        "Expected changed flags to be 1, got %i\n",
                        event->player_flags_changed.flags);
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

        if (!check_event(harness,
                         VSX_CONNECTION_EVENT_TYPE_PLAYER_NAME_CHANGED,
                         check_player_name_changed_cb,
                         name_header,
                         sizeof name_header - 1,
                         NULL))
                return false;

        if (!check_event(harness,
                         VSX_CONNECTION_EVENT_TYPE_PLAYER_FLAGS_CHANGED,
                         check_player_flags_changed_cb,
                         data_header,
                         sizeof data_header - 1,
                         NULL))
                return false;

        return true;
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

        if (!send_conversation_id(harness))
                goto error;

        if (!send_player_data(harness))
                goto error;

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
read_reconnect_message(struct harness *harness,
                       int n_messages)
{
        uint8_t reconnect_message[] =
                "\x82\x0b\x81ghijklmn\x02\x00";

        reconnect_message[sizeof reconnect_message - 3] = n_messages;

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

        if (!read_reconnect_message(harness, 2)) {
                ret = false;
                goto out;
        }

out:
        free_harness(harness);

        return ret;
}

static bool
test_reset_connect_timeout_for_stable_connection(struct harness *harness)
{
        if (!wake_up_connection(harness) ||
            !accept_connection(harness) ||
            !read_ws_request(harness) ||
            !write_string(harness, "\r\n\r\n") ||
            !read_reconnect_message(harness, 2) ||
            !send_player_id_no_event(harness))
                return false;

        /* Advance time by 15 seconds so that the vsx_connection will
         * decide that the connection was stable.
         */
        replacement_monotonic_time += 15 * 1000 * 1000;

        /* Now it should go back to trying to reconnect immediately */
        if (!do_unexpected_close(harness) ||
            !wake_up_connection(harness) ||
            !accept_connection(harness))
                return false;

        return true;
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

                if (!read_reconnect_message(harness, 2)) {
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

        if (!test_reset_connect_timeout_for_stable_connection(harness)) {
                ret = false;
                goto out;
        }

out:
        replace_monotonic_time = false;
        free_harness(harness);

        return ret;
}

static bool
test_reconnect_pending_data(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        bool ret = true;

        /* Send an incomplete message so that the data will be pending
         * in the input buffer of the connection. The message
         * deliberately contains the websocket terminator so that if
         * the pending data isn’t cleared then it will confuse the
         * part that skips the websocket header.
         */
        if (!write_data(harness,
                        (const uint8_t *) "\x82\x08\x01\r\n\r\n",
                        7)) {
                ret = false;
                goto out;
        }

        if (!do_unexpected_close(harness) ||
            !wake_up_connection(harness) ||
            !accept_connection(harness) ||
            !read_ws_request(harness) ||
            !write_string(harness, "\r\n\r\n") ||
            !read_reconnect_message(harness, 0)) {
                ret = false;
                goto out;
        }

        /* Send any message that would trigger an event to check that
         * the connection is correctly processing messages.
         */

        if (!send_player_id(harness)) {
                ret = false;
                goto out;
        }

out:
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
check_n_tiles_changed_cb(struct harness *harness,
                         const struct vsx_connection_event *event,
                         void *user_data)
{
        if (event->n_tiles_changed.n_tiles != 0x42) {
                fprintf(stderr,
                        "n_tiles in event has unexpected value (%i != %i)\n",
                        event->n_tiles_changed.n_tiles,
                        0x42);
                return false;
        }

        return true;
}

static bool
test_send_n_tiles(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        bool ret = true;

        if (!check_event(harness,
                         VSX_CONNECTION_EVENT_TYPE_N_TILES_CHANGED,
                         check_n_tiles_changed_cb,
                         (const uint8_t *) "\x82\x02\x02\x42", 4,
                         NULL /* user_data */)) {
                ret = false;
                goto out;
        }

out:
        free_harness(harness);

        return ret;
}

static bool
check_language_changed_cb(struct harness *harness,
                          const struct vsx_connection_event *event,
                          void *user_data)
{
        if (strcmp(event->language_changed.code, "fr")) {
                fprintf(stderr,
                        "language in event has unexpected value\n"
                        " Expected: fr\n"
                        " Received: %s\n",
                        event->language_changed.code);
                return false;
        }

        return true;
}

static bool
test_send_language(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        bool ret = true;

        if (!check_event(harness,
                         VSX_CONNECTION_EVENT_TYPE_LANGUAGE_CHANGED,
                         check_language_changed_cb,
                         (const uint8_t *) "\x82\x04\x0C" "fr\x00", 6,
                         NULL /* user_data */)) {
                ret = false;
                goto out;
        }

out:
        free_harness(harness);

        return ret;
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
add_player(struct harness *harness)
{
        static const uint8_t add_player_message[] =
                "\x82\x09\x04\x01George\x00";

        return check_event(harness,
                           VSX_CONNECTION_EVENT_TYPE_PLAYER_NAME_CHANGED,
                           check_player_added_cb,
                           add_player_message,
                           sizeof add_player_message - 1,
                           NULL /* user_data */);
}

static bool
check_shouter_num(int expected_shouter,
                  const struct vsx_connection_event *event)
{
        if (expected_shouter != event->player_shouted.player_num) {
                fprintf(stderr,
                        "Expected shouter to be %i but got %i\n",
                        expected_shouter,
                        event->player_shouted.player_num);
                return false;
        }

        return true;
}

static bool
check_self_shouted_cb(struct harness *harness,
                      const struct vsx_connection_event *event,
                      void *user_data)
{
        return check_shouter_num(0, event);
}

static bool
check_other_shouted_cb(struct harness *harness,
                       const struct vsx_connection_event *event,
                       void *user_data)
{
        return check_shouter_num(1, event);
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

        replacement_monotonic_time = vsx_monotonic_get();
        replace_monotonic_time = true;

        if (!check_event_with_ignore(harness,
                                     VSX_CONNECTION_EVENT_TYPE_PLAYER_SHOUTED,
                                     VSX_CONNECTION_EVENT_TYPE_POLL_CHANGED,
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

        if (!check_event_with_ignore(harness,
                                     VSX_CONNECTION_EVENT_TYPE_PLAYER_SHOUTED,
                                     VSX_CONNECTION_EVENT_TYPE_POLL_CHANGED,
                                     check_other_shouted_cb,
                                     other_shout_message,
                                     sizeof other_shout_message - 1,
                                     NULL /* user_data */)) {
                ret = false;
                goto out;
        }

out:
        replace_monotonic_time = false;
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
        buf[3] = (char) (payload_length & 0xff);
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
                               i /* player */)) {
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

static bool
test_set_n_tiles(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        bool ret = true;

        vsx_connection_set_n_tiles(harness->connection, 0x82);
        vsx_connection_set_n_tiles(harness->connection, 0x42);

        const uint8_t expected_data[] =
                "\x82\x02\x8b\x42";

        if (!expect_data(harness, expected_data, sizeof expected_data - 1)) {
                ret = false;
                goto out;
        }

        if (fd_ready_for_read(harness->server_fd)) {
                fprintf(stderr,
                        "Connection sent more data after set_n_tiles "
                        "command\n");
                ret = false;
                goto out;
        }

out:
        free_harness(harness);

        return ret;
}

static bool
test_set_language(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        bool ret = true;

        vsx_connection_set_language(harness->connection, "en");
        vsx_connection_set_language(harness->connection,
                                    "really_excessively_long_language_code");
        vsx_connection_set_language(harness->connection, "fr");

        const uint8_t expected_data[] =
                "\x82\x04\x8e" "fr\x0";

        if (!expect_data(harness, expected_data, sizeof expected_data - 1)) {
                ret = false;
                goto out;
        }

        if (fd_ready_for_read(harness->server_fd)) {
                fprintf(stderr,
                        "Connection sent more data after set_language "
                        "command\n");
                ret = false;
                goto out;
        }

out:
        free_harness(harness);

        return ret;
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

out:
        free_harness(harness);

        return ret;
}

struct check_add_all_player_name_closure {
        int player_num;
};

static bool
check_add_all_player_name_cb(struct harness *harness,
                             const struct vsx_connection_event *event,
                             void *user_data)
{
        struct check_add_all_player_name_closure *closure = user_data;

        if (event->player_name_changed.player_num != closure->player_num) {
                fprintf(stderr,
                        "Changed player num does not match (%i != %i)\n",
                        event->player_name_changed.player_num,
                        closure->player_num);
                return false;
        }

        struct vsx_buffer buf = VSX_BUFFER_STATIC_INIT;
        vsx_buffer_append_printf(&buf, "Player %i", closure->player_num);

        int comp = strcmp((const char *) buf.data,
                          event->player_name_changed.name);

        vsx_buffer_destroy(&buf);

        if (comp) {
                fprintf(stderr,
                        "Changed player name does not match expected\n"
                        " Expected: Player %i\n"
                        " Received: %s\n",
                        closure->player_num,
                        event->player_name_changed.name);
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
                int player_num = ((i & 0xfc) |
                                  ((i & 2) >> 1) |
                                  ((i & 1) << 1));

                vsx_buffer_set_length(&buf, 0);
                vsx_buffer_append_string(&buf, "\x82\xff\x04\xff");
                vsx_buffer_append_printf(&buf, "Player %i", player_num);
                buf.length++;
                buf.data[1] = buf.length - 2;
                buf.data[3] = player_num;

                struct check_add_all_player_name_closure closure = {
                        .player_num = player_num,
                };

                if (!check_event(harness,
                                 VSX_CONNECTION_EVENT_TYPE_PLAYER_NAME_CHANGED,
                                 check_add_all_player_name_cb,
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

out:
        vsx_buffer_destroy(&buf);
        free_harness(harness);

        return ret;
}

struct check_synced_closure {
        bool synced;
};

static bool
check_synced_cb(struct harness *harness,
                const struct vsx_connection_event *event,
                void *user_data)
{
        struct check_synced_closure *closure = user_data;

        closure->synced = event->synced;

        return true;
}

static bool
check_synced(struct harness *harness, bool *synced)
{
        struct check_synced_closure closure;

        /* Change a player name so that we can check the synced value
         * in the corresponding event.
         */
        if (!check_event(harness,
                         VSX_CONNECTION_EVENT_TYPE_PLAYER_NAME_CHANGED,
                         check_synced_cb,
                         (const uint8_t *) "\x82\x05\x04\x00!!\x00", 7,
                         &closure))
                return false;

        *synced = closure.synced;

        return true;
}

static bool
test_sync(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        bool ret = true;

        for (int i = 0; i < 2; i++) {
                bool synced;

                if (!check_synced(harness, &synced)) {
                        ret = false;
                        goto out;
                }

                /* A new connection shouldn’t be synced */
                if (synced) {
                        fprintf(stderr,
                                "Newly %s connection is already synced\n",
                                i == 0 ? "created" : "reconnected");
                        ret = false;
                        goto out;
                }

                if (!write_string(harness, "\x82\x01\x07")) {
                        ret = false;
                        goto out;
                }

                if (!check_synced(harness, &synced)) {
                        ret = false;
                        goto out;
                }

                if (!synced) {
                        fprintf(stderr,
                                "Connection is not synced after sending sync "
                                "command\n");
                        ret = false;
                        goto out;
                }

                if (i > 0)
                        break;

                if (!do_unexpected_close(harness) ||
                    !wake_up_connection(harness) ||
                    !accept_connection(harness) ||
                    !read_ws_request(harness) ||
                    !write_string(harness, "\r\n\r\n") ||
                    !read_reconnect_message(harness, 0)) {
                        ret = false;
                        goto out;
                }
        }

out:
        free_harness(harness);

        return ret;
}

static bool
check_end_cb(struct harness *harness,
             const struct vsx_connection_event *event,
             void *user_data)
{
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
                         VSX_CONNECTION_EVENT_TYPE_END,
                         check_end_cb,
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

static bool
test_get_person_id(void)
{
        struct harness *harness = create_harness();

        if (harness == NULL)
                return NULL;

        bool ret = true;

        if (!read_ws_request(harness) ||
            !write_string(harness, "\r\n\r\n") ||
            !read_new_player_request(harness)) {
                ret = false;
                goto out;
        }

        uint64_t person_id = UINT64_MAX;

        if (vsx_connection_get_person_id(harness->connection,
                                         &person_id)) {
                fprintf(stderr,
                        "Person ID is already available before header was "
                        "sent.\n");
                ret = false;
                goto out;
        }

        if (!send_player_id(harness)) {
                ret = false;
                goto out;
        }

        if (!vsx_connection_get_person_id(harness->connection,
                                          &person_id)) {
                fprintf(stderr,
                        "Person ID is not available even after sending the "
                        "header.\n");
                ret = false;
                goto out;
        }

        const uint64_t expected_id = UINT64_C(0x6e6d6c6b6a696867);

        if (person_id != expected_id) {
                fprintf(stderr,
                        "Person ID is not as expected.\n"
                        " Expected: 0x%" PRIx64 "\n"
                        " Received: 0x%" PRIx64 "\n",
                        expected_id,
                        person_id);
                ret = false;
                goto out;
        }

out:
        free_harness(harness);
        return ret;
}

static bool
test_set_person_id(void)
{
        struct harness *harness = create_harness_no_start();

        if (harness == NULL)
                return NULL;

        bool ret = true;

        uint64_t expected_id = 0xfedcba9876543210;

        vsx_connection_set_person_id(harness->connection, expected_id);

        uint64_t received_id;

        if (!vsx_connection_get_person_id(harness->connection,
                                          &received_id)) {
                fprintf(stderr, "Failed to get person ID after setting it.\n");
                ret = false;
                goto out;
        }

        if (received_id != expected_id) {
                fprintf(stderr,
                        "Person ID not as set:\n"
                        " Expected 0x%" PRIx64 "\n"
                        " Received 0x%" PRIx64 "\n",
                        expected_id,
                        received_id);
                ret = false;
                goto out;
        }

        if (!start_connection(harness) ||
            !read_ws_request(harness) ||
            !write_string(harness, "\r\n\r\n")) {
                ret = false;
                goto out;
        }

        /* Make sure that the connection sends a reconnect command
         * with the chosen person ID instead of trying to create a new
         * person.
         */
        const uint8_t expected_data[] =
                "\x82\x0b\x81\x10\x32\x54\x76\x98\xba\xdc\xfe\x00\x00";

        if (!expect_data(harness, expected_data, (sizeof expected_data) - 1)) {
                ret = false;
                goto out;
        }

        /* Make sure that we can can’t change the person ID after it
         * is set once.
         */
        vsx_connection_set_person_id(harness->connection, 5);

        received_id = 0;

        if (!vsx_connection_get_person_id(harness->connection,
                                          &received_id)) {
                fprintf(stderr, "Failed to get person ID after setting it.\n");
                ret = false;
                goto out;
        }

        if (received_id != expected_id) {
                fprintf(stderr,
                        "Person ID changed after setting it a second time:\n"
                        " Expected 0x%" PRIx64 "\n"
                        " Received 0x%" PRIx64 "\n",
                        expected_id,
                        received_id);
                ret = false;
                goto out;
        }

out:
        free_harness(harness);
        return ret;
}

static bool
check_after_bad_thing_error(struct harness *harness)
{
        /* The connection should close the write end of the socket */
        uint8_t byte;
        int got = read(harness->server_fd, &byte, 1);

        if (got != 0) {
                fprintf(stderr,
                        "Expected connection to close, got %i bytes\n",
                        got);
                return false;
        }

        shutdown(harness->server_fd, SHUT_WR);

        if (!wake_up_connection(harness))
                return false;

        if (harness->wakeup_time != INT64_MAX) {
                fprintf(stderr,
                        "Expected connection to block forever after "
                        "bad player ID, but got timeout of %f seconds\n",
                        (harness->wakeup_time -
                         vsx_monotonic_get()) /
                        1000000.0f);
                return false;
        }

        if (harness->poll_fd != -1) {
                fprintf(stderr,
                        "Expected connection to close fd, but it still has "
                        "a poll fd\n");
                return false;
        }

        return true;
}

static bool
test_bad_player_id(void)
{
        struct harness *harness = create_harness_no_start();

        if (harness == NULL)
                return NULL;

        bool ret = true;

        vsx_connection_set_person_id(harness->connection,
                                     UINT64_C(0xfedcba9876543210));

        if (!start_connection(harness) ||
            !read_ws_request(harness) ||
            !write_string(harness, "\r\n\r\n")) {
                ret = false;
                goto out;
        }

        const uint8_t expected_data[] =
                "\x82\x0b\x81\x10\x32\x54\x76\x98\xba\xdc\xfe\x00\x00";

        if (!expect_data(harness, expected_data, (sizeof expected_data) - 1)) {
                ret = false;
                goto out;
        }

        harness->expected_error_domain = &vsx_connection_error;
        harness->expected_error_code = VSX_CONNECTION_ERROR_BAD_PLAYER_ID;
        harness->expected_error_message =
                "The player ID no longer exists";

        if (!write_data(harness,
                        (const uint8_t *) "\x82\x01\x09", 3)) {
                ret = false;
                goto out;
        }

        if (harness->expected_error_message != NULL) {
                fprintf(stderr,
                        "No error received after sending bad player ID "
                        "message\n");
                ret = false;
                goto out;
        }

        if (!check_after_bad_thing_error(harness)) {
                ret = false;
                goto out;
        }

out:
        free_harness(harness);
        return ret;
}

static bool
test_join_error(enum vsx_connection_error error_code,
                const char *error_message,
                uint8_t protocol_code)
{
        struct harness *harness = create_harness_no_start();

        if (harness == NULL)
                return NULL;

        bool ret = true;

        vsx_connection_set_conversation_id(harness->connection,
                                           UINT64_C(0xfedcba9876543210));

        if (!start_connection(harness) ||
            !read_ws_request(harness) ||
            !write_string(harness, "\r\n\r\n")) {
                ret = false;
                goto out;
        }

        const uint8_t expected_data[] =
                "\x82\x15\x8d\x10\x32\x54\x76\x98\xba\xdc\xfe" "test_player\0";

        if (!expect_data(harness, expected_data, (sizeof expected_data) - 1)) {
                ret = false;
                goto out;
        }

        harness->expected_error_domain = &vsx_connection_error;
        harness->expected_error_code = error_code;
        harness->expected_error_message = error_message;

        uint8_t command[] = {
                0x82, 0x01, protocol_code,
        };

        if (!write_data(harness, command, sizeof command)) {
                ret = false;
                goto out;
        }

        if (harness->expected_error_message != NULL) {
                fprintf(stderr,
                        "No error received after sending conversation ID error "
                        "message\n");
                ret = false;
                goto out;
        }

        if (!check_after_bad_thing_error(harness)) {
                ret = false;
                goto out;
        }

out:
        free_harness(harness);
        return ret;
}

static bool
test_bad_conversation_id(void)
{
        return test_join_error(VSX_CONNECTION_ERROR_BAD_CONVERSATION_ID,
                               "The conversation ID no longer exists",
                               0x0b);
}

static bool
test_conversation_full(void)
{
        return test_join_error(VSX_CONNECTION_ERROR_CONVERSATION_FULL,
                               "The conversation is full",
                               0x0d);
}

static bool
test_connection_is_blocking_for_config(struct harness *harness)
{
        if (!wake_up_connection(harness))
                return false;

        if (harness->poll_fd != -1) {
                fprintf(stderr,
                        "Expected harness to be waiting for config but it "
                        "has a poll FD.\n");
                return false;
        }

        if (harness->wakeup_time != INT64_MAX) {
                fprintf(stderr,
                        "Expected harness to be waiting for config it it "
                        "has a timeout in %f seconds.\n",
                        (harness->wakeup_time -
                         vsx_monotonic_get()) /
                        1000000.0f);
                return false;
        }

        return true;
}

static bool
test_address_block_connect(void)
{
        struct harness *harness = create_harness_no_start();

        if (harness == NULL)
                return false;

        vsx_connection_set_room(harness->connection, "test_room");
        vsx_connection_set_player_name(harness->connection, "test_player");

        vsx_connection_set_running(harness->connection, true);

        bool ret = true;

        if (!test_connection_is_blocking_for_config(harness)) {
                ret = false;
                goto out;
        }

        vsx_connection_set_address(harness->connection,
                                   &harness->local_address);

        if (!wake_up_and_accept_connection(harness)) {
                ret = false;
                goto out;
        }

out:
        free_harness(harness);
        return ret;
}

static bool
test_player_name_block_connect(void)
{
        struct harness *harness = create_harness_no_start();

        if (harness == NULL)
                return false;

        vsx_connection_set_address(harness->connection,
                                   &harness->local_address);

        vsx_connection_set_running(harness->connection, true);

        bool ret = true;

        if (!test_connection_is_blocking_for_config(harness)) {
                ret = false;
                goto out;
        }

        vsx_connection_set_player_name(harness->connection, "test_player");

        if (!wake_up_and_accept_connection(harness)) {
                ret = false;
                goto out;
        }

out:
        free_harness(harness);
        return ret;
}

static bool
test_new_private_game(void)
{
        struct harness *harness = create_harness_no_start();

        if (harness == NULL)
                return false;

        vsx_connection_set_address(harness->connection,
                                   &harness->local_address);
        vsx_connection_set_player_name(harness->connection, "test_player");

        vsx_connection_set_running(harness->connection, true);

        bool ret = true;

        if (!wake_up_and_accept_connection(harness)) {
                ret = false;
                goto out;
        }

        if (!read_ws_request(harness) ||
            !write_string(harness, "\r\n\r\n")) {
                ret = false;
                goto out;
        }

        if (!expect_data(harness,
                         (const uint8_t *) "\x82\x0e\x8c\0test_player\0",
                         16)) {
                ret = false;
                goto out;
        }

out:
        free_harness(harness);
        return ret;
}

static bool
test_join_private_game(void)
{
        struct harness *harness = create_harness_no_start();

        if (harness == NULL)
                return false;

        vsx_connection_set_address(harness->connection,
                                   &harness->local_address);
        vsx_connection_set_player_name(harness->connection, "test_player");

        vsx_connection_set_conversation_id(harness->connection,
                                           0x8081828384858687);

        vsx_connection_set_running(harness->connection, true);

        bool ret = true;

        if (!wake_up_and_accept_connection(harness)) {
                ret = false;
                goto out;
        }

        if (!read_ws_request(harness) ||
            !write_string(harness, "\r\n\r\n")) {
                ret = false;
                goto out;
        }

        if (!expect_data(harness,
                         (const uint8_t *)
                         "\x82\x15\x8d"
                         "\x87\x86\x85\x84\x83\x82\x81\x80"
                         "test_player\0",
                         23)) {
                ret = false;
                goto out;
        }

out:
        free_harness(harness);
        return ret;
}

static bool
test_stop_running(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        bool ret = true;

        harness->events_triggered = 0;

        vsx_connection_set_running(harness->connection, true);

        if (harness->events_triggered != 0) {
                fprintf(stderr,
                        "Events received after setting running state to same "
                        "value.\n");
                ret = false;
                goto out;
        }

        vsx_connection_set_running(harness->connection, false);

        if (harness->events_triggered !=
            ((1 << VSX_CONNECTION_EVENT_TYPE_RUNNING_STATE_CHANGED) |
             (1 << VSX_CONNECTION_EVENT_TYPE_POLL_CHANGED))) {
                fprintf(stderr,
                        "Expected running state changed and poll changed "
                        "events but got event mask 0x%x\n",
                        harness->events_triggered);
                ret = false;
                goto out;
        }

        if (harness->poll_fd != -1) {
                fprintf(stderr,
                        "Connection has a poll fd after stopping running.\n");
                ret = false;
                goto out;
        }

        harness->events_triggered = 0;

        vsx_connection_set_running(harness->connection, false);

        if (harness->events_triggered != 0) {
                fprintf(stderr,
                        "Events received after setting running state to same "
                        "value.\n");
                ret = false;
                goto out;
        }

        vsx_connection_set_running(harness->connection, true);

        if (harness->events_triggered !=
            ((1 << VSX_CONNECTION_EVENT_TYPE_RUNNING_STATE_CHANGED) |
             (1 << VSX_CONNECTION_EVENT_TYPE_POLL_CHANGED))) {
                fprintf(stderr,
                        "Expected running state changed and poll changed "
                        "events but got event mask 0x%x\n",
                        harness->events_triggered);
                ret = false;
                goto out;
        }

        if (!wake_up_and_accept_connection(harness)) {
                ret = false;
                goto out;
        }

out:
        free_harness(harness);
        return ret;
}

static bool
check_message_cb(struct harness *harness,
                 const struct vsx_connection_event *event,
                 void *user_data)
{
        if (strcmp(event->message.message, "gh")) {
                fprintf(stderr,
                        "Message mismatch:\n"
                        " Expected: gh\n"
                        " Received: %s\n",
                        event->message.message);
                return false;
        }

        return true;
}

static bool
test_reset(void)
{
        struct harness *harness = create_negotiated_harness();

        if (harness == NULL)
                return false;

        bool ret = true;

        /* Send a message to increase the next_message_num so we can
         * check that it gets reset.
         */
        if (!check_event(harness,
                         VSX_CONNECTION_EVENT_TYPE_MESSAGE,
                         check_message_cb,
                         (const uint8_t *)
                         "\x82\x05\x01\x0gh\0",
                         7,
                         NULL /* user_data */)) {
                ret = false;
                goto out;
        }

        /* Queue up a bunch of state so that we can test that it won’t
         * be sent after we reset the connection.
         */
        vsx_connection_set_typing(harness->connection, true);
        vsx_connection_shout(harness->connection);
        vsx_connection_turn(harness->connection);
        vsx_connection_move_tile(harness->connection, 0, 1, 2);
        vsx_connection_set_n_tiles(harness->connection, 8);
        vsx_connection_set_language(harness->connection, "fr");
        vsx_connection_send_message(harness->connection,
                                    "Manĝu terpomojn");

        harness->events_triggered = 0;

        vsx_connection_reset(harness->connection);

        if (harness->events_triggered !=
            ((1 << VSX_CONNECTION_EVENT_TYPE_RUNNING_STATE_CHANGED) |
             (1 << VSX_CONNECTION_EVENT_TYPE_POLL_CHANGED))) {
                fprintf(stderr,
                        "Expected running state changed and poll changed "
                        "events but got event mask 0x%x\n",
                        harness->events_triggered);
                ret = false;
                goto out;
        }

        if (vsx_connection_get_running(harness->connection)) {
                fprintf(stderr,
                        "Connection is running after reset");
                ret = false;
                goto out;
        }

        if (harness->poll_fd != -1) {
                fprintf(stderr,
                        "Connection has a poll fd after reset.\n");
                ret = false;
                goto out;
        }

        uint64_t person_id;

        if (vsx_connection_get_person_id(harness->connection,
                                         &person_id)) {
                fprintf(stderr,
                        "Connection has a person ID after reset\n");
                ret = false;
                goto out;
        }

        vsx_connection_set_running(harness->connection, true);

        if (!test_connection_is_blocking_for_config(harness)) {
                ret = false;
                goto out;
        }

        vsx_connection_set_player_name(harness->connection, "test_player");

        if (!wake_up_and_accept_connection(harness)) {
                ret = false;
                goto out;
        }

        if (!read_ws_request(harness) ||
            !write_string(harness, "\r\n\r\n")) {
                ret = false;
                goto out;
        }

        if (!expect_data(harness,
                         (const uint8_t *)
                         "\x82\x0e\x8c"
                         "\x0" /* empty language code */
                         "test_player\0",
                         16)) {
                ret = false;
                goto out;
        }

        /* The connection shouldn’t have any other data to send */
        if ((harness->poll_events & POLLOUT) ||
            fd_ready_for_read(harness->server_fd)) {
                fprintf(stderr,
                        "Connection wants to send more data after header.\n");
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

        if (!test_frame_errors())
                ret = EXIT_FAILURE;

        if (!test_slow_ws_response())
                ret = EXIT_FAILURE;

        if (!test_immediate_reconnect())
                ret = EXIT_FAILURE;

        if (!test_reconnect_delay())
                ret = EXIT_FAILURE;

        if (!test_reconnect_pending_data())
                ret = EXIT_FAILURE;

        if (!test_keep_alive())
                ret = EXIT_FAILURE;

        if (!test_send_n_tiles())
                ret = EXIT_FAILURE;

        if (!test_send_language())
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

        if (!test_set_n_tiles())
                ret = EXIT_FAILURE;

        if (!test_set_language())
                ret = EXIT_FAILURE;

        if (!test_send_all_tiles())
                ret = EXIT_FAILURE;

        if (!test_send_all_players())
                ret = EXIT_FAILURE;

        if (!test_sync())
                ret = EXIT_FAILURE;

        if (!test_end(true /* do_shutdown */))
                ret = EXIT_FAILURE;

        if (!test_end(false /* do_shutdown */))
                ret = EXIT_FAILURE;

        if (!test_read_error())
                ret = EXIT_FAILURE;

        if (!test_write_buffer_full())
                ret = EXIT_FAILURE;

        if (!test_get_person_id())
                ret = EXIT_FAILURE;

        if (!test_set_person_id())
                ret = EXIT_FAILURE;

        if (!test_bad_player_id())
                ret = EXIT_FAILURE;

        if (!test_bad_conversation_id())
                ret = EXIT_FAILURE;

        if (!test_conversation_full())
                ret = EXIT_FAILURE;

        if (!test_leak_pendings())
                ret = EXIT_FAILURE;

        if (!test_address_block_connect())
                ret = EXIT_FAILURE;

        if (!test_player_name_block_connect())
                ret = EXIT_FAILURE;

        if (!test_new_private_game())
                ret = EXIT_FAILURE;

        if (!test_join_private_game())
                ret = EXIT_FAILURE;

        if (!test_stop_running())
                ret = EXIT_FAILURE;

        if (!test_reset())
                ret = EXIT_FAILURE;

        return ret;
}
