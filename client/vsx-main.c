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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>

#include "vsx-connection.h"
#include "vsx-utf8.h"
#include "vsx-netaddress.h"
#include "vsx-monotonic.h"
#include "vsx-buffer.h"
#include "vsx-worker.h"

struct vsx_main_data {
        struct vsx_connection *connection;
        struct vsx_worker *worker;

        struct vsx_listener event_listener;

        int wakeup_fds[2];

        pthread_mutex_t mutex;

        /* The following state is protected by the mutex */

        bool wakeup_queued;

        bool should_quit;

        struct vsx_buffer log_buffer;
        struct vsx_buffer alternate_log_buffer;
};

VSX_PRINTF_FORMAT(2, 3)
static void
format_print(struct vsx_main_data *main_data,
             const char *format,
             ...);

static char *option_server = "gemelo.org";
int option_server_port = 5144;
static char *option_room = "default";
static char *option_player_name = NULL;

static const char options[] = "-hs:p:r:n:";

static void
usage(void)
{
        printf("verda-sxtelo-client - An anagram game in Esperanto "
               "for the web\n"
               "usage: verda-sxtelo-client [options]...\n"
               " -h                   Show this help message\n"
               " -s <hostname>        The name of the server to connect to\n"
               " -p <port>            The port on the server to connect to\n"
               " -r <room>            The room to connect to\n"
               " -n <player>          The player name\n");
}

static bool
process_arguments(int argc, char **argv)
{
        int opt;

        opterr = false;

        while ((opt = getopt(argc, argv, options)) != -1) {
                switch (opt) {
                case ':':
                case '?':
                        fprintf(stderr, "invalid option '%c'\n", optopt);
                        return false;

                case '\1':
                        fprintf(stderr, "unexpected argument \"%s\"\n", optarg);
                        return false;

                case 'h':
                        usage();
                        return false;

                case 's':
                        option_server = optarg;
                        break;

                case 'p':
                        option_server_port = strtoul(optarg, NULL, 10);
                        break;

                case 'r':
                        option_room = optarg;
                        break;

                case 'n':
                        option_player_name = optarg;
                        break;
                }
        }

        return true;
}

static void
wake_up_locked(struct vsx_main_data *main_data)
{
        if (main_data->wakeup_queued)
                return;

        static const uint8_t byte = 'W';
        write(main_data->wakeup_fds[1], &byte, 1);
        main_data->wakeup_queued = true;
}

static void
handle_error(struct vsx_main_data *main_data,
             const struct vsx_connection_event *event)
{
        format_print(main_data, "error: %s\n", event->error.error->message);
}

static void
handle_running_state_changed(struct vsx_main_data *main_data,
                             const struct vsx_connection_event *event)
{
        if (!event->running_state_changed.running) {
                pthread_mutex_lock(&main_data->mutex);
                wake_up_locked(main_data);
                main_data->should_quit = true;
                pthread_mutex_unlock(&main_data->mutex);
        }
}

static void
handle_message(struct vsx_main_data *main_data,
               const struct vsx_connection_event *event)
{
        format_print(main_data,
                     "%s: %s\n",
                     vsx_player_get_name(event->message.player),
                     event->message.message);
}

static void
handle_player_shouted(struct vsx_main_data *main_data,
                      const struct vsx_connection_event *event)
{

        const struct vsx_player *player = event->player_shouted.player;

        format_print(main_data, "** %s SHOUTS\n", vsx_player_get_name(player));
}

static void
handle_n_tiles(struct vsx_main_data *main_data,
               const struct vsx_connection_event *event)
{

        int n_tiles = event->n_tiles_changed.n_tiles;

        format_print(main_data, "** number of tiles is %i\n", n_tiles);
}

static void
handle_tile_changed(struct vsx_main_data *main_data,
                    const struct vsx_connection_event *event)
{
        char letter[7];
        int letter_len;

        const struct vsx_tile *tile = event->tile_changed.tile;

        letter_len = vsx_utf8_encode(vsx_tile_get_letter(tile), letter);
        letter[letter_len] = '\0';

        format_print(main_data,
                     "%s: %i (%i,%i) %s\n",
                     event->tile_changed.new_tile ? "new_tile" : "tile changed",
                     vsx_tile_get_number(tile),
                     vsx_tile_get_x(tile), vsx_tile_get_y(tile),
                     letter);
}

static void
print_state_message(struct vsx_main_data *main_data)
{
        switch (vsx_connection_get_state(main_data->connection)) {
        case VSX_CONNECTION_STATE_AWAITING_HEADER:
                break;

        case VSX_CONNECTION_STATE_IN_PROGRESS:
                format_print(main_data,
                             "You are now in a conversation with a stranger. "
                             "Say hi!\n");
                break;

        case VSX_CONNECTION_STATE_DONE:
                format_print(main_data,
                             "The conversation has finished\n");
                break;
        }
}

static void
handle_state_changed(struct vsx_main_data *main_data,
                     const struct vsx_connection_event *event)
{
        print_state_message(main_data);
}

static void
event_cb(struct vsx_listener *listener,
         void *data)
{
        struct vsx_main_data *main_data =
                vsx_container_of(listener,
                                 struct vsx_main_data,
                                 event_listener);
        const struct vsx_connection_event *event = data;

        switch (event->type) {
        case VSX_CONNECTION_EVENT_TYPE_ERROR:
                handle_error(main_data, event);
                break;
        case VSX_CONNECTION_EVENT_TYPE_MESSAGE:
                handle_message(main_data, event);
                break;
        case VSX_CONNECTION_EVENT_TYPE_PLAYER_SHOUTED:
                handle_player_shouted(main_data, event);
                break;
        case VSX_CONNECTION_EVENT_TYPE_N_TILES_CHANGED:
                handle_n_tiles(main_data, event);
                break;
        case VSX_CONNECTION_EVENT_TYPE_TILE_CHANGED:
                handle_tile_changed(main_data, event);
                break;
        case VSX_CONNECTION_EVENT_TYPE_RUNNING_STATE_CHANGED:
                handle_running_state_changed(main_data, event);
                break;
        case VSX_CONNECTION_EVENT_TYPE_STATE_CHANGED:
                handle_state_changed(main_data, event);
                break;
        case VSX_CONNECTION_EVENT_TYPE_PLAYER_CHANGED:
        case VSX_CONNECTION_EVENT_TYPE_POLL_CHANGED:
                break;
        }
}

static void
format_print(struct vsx_main_data *main_data,
             const char *format,
             ...)
{
        va_list ap;

        va_start(ap, format);

        pthread_mutex_lock(&main_data->mutex);

        vsx_buffer_append_vprintf(&main_data->log_buffer, format, ap);

        wake_up_locked(main_data);

        pthread_mutex_unlock(&main_data->mutex);

        va_end(ap);
}

static bool
lookup_address(const char *hostname, int port, struct vsx_netaddress *address)
{
        struct addrinfo *addrinfo;

        int ret = getaddrinfo(hostname,
                              NULL, /* service */
                              NULL, /* hints */
                              &addrinfo);

        if (ret)
                return false;

        bool found = false;

        for (const struct addrinfo * a = addrinfo; a; a = a->ai_next) {
                switch (a->ai_family) {
                case AF_INET:
                        if (a->ai_addrlen != sizeof(struct sockaddr_in))
                                continue;
                        break;
                case AF_INET6:
                        if (a->ai_addrlen != sizeof(struct sockaddr_in6))
                                continue;
                        break;
                default:
                        continue;
                }

                struct vsx_netaddress_native native_address;

                memcpy(&native_address.sockaddr, a->ai_addr, a->ai_addrlen);
                native_address.length = a->ai_addrlen;

                vsx_netaddress_from_native(address, &native_address);
                address->port = port;

                found = true;
                break;
        }

        freeaddrinfo(addrinfo);

        return found;
}

static void
handle_log_locked(struct vsx_main_data *main_data)
{
        if (main_data->log_buffer.length <= 0)
                return;

        struct vsx_buffer tmp = main_data->log_buffer;
        main_data->log_buffer = main_data->alternate_log_buffer;
        main_data->alternate_log_buffer = tmp;

        vsx_buffer_set_length(&main_data->log_buffer, 0);

        pthread_mutex_unlock(&main_data->mutex);

        fwrite(main_data->alternate_log_buffer.data,
               1,
               main_data->alternate_log_buffer.length,
               stdout);

        pthread_mutex_lock(&main_data->mutex);
}

static void
run_main_loop(struct vsx_main_data *main_data)
{
        struct pollfd fds[1];

        fds[0].fd = main_data->wakeup_fds[0];
        fds[0].events = POLLIN;

        pthread_mutex_lock(&main_data->mutex);

        while (!main_data->should_quit) {
                for (int i = 0; i < VSX_N_ELEMENTS(main_data->wakeup_fds); i++)
                        fds[i].revents = 0;

                pthread_mutex_unlock(&main_data->mutex);

                if (poll(fds, 1 /* nfds */, -1 /* timeout */) == -1) {
                        fprintf(stderr, "poll failed: %s\n", strerror(errno));
                        return;
                }

                pthread_mutex_lock(&main_data->mutex);

                if (fds[0].revents) {
                        uint8_t byte;

                        int got = read(main_data->wakeup_fds[0], &byte, 1);

                        if (got == 0) {
                                fprintf(stderr,
                                        "Unexpected EOF on wakeup fd\n");
                                break;
                        } else if (got == -1 && errno != EINTR) {
                                fprintf(stderr,
                                        "Error reading log wakeup fd: %s\n",
                                        strerror(errno));
                                break;
                        } else {
                                main_data->wakeup_queued = false;
                        }
                }

                handle_log_locked(main_data);
        }

        pthread_mutex_unlock(&main_data->mutex);
}

static struct vsx_connection *
create_connection(void)
{
        struct vsx_netaddress address;

        address.port = option_server_port;

        if (!vsx_netaddress_from_string(&address,
                                        option_server, option_server_port) &&
            !lookup_address(option_server, option_server_port, &address)) {
                fprintf(stderr, "Failed to resolve %s\n", option_server);
                return NULL;
        }

        const char *player_name = option_player_name;

        if (player_name == NULL) {
                player_name = getlogin();
                if (player_name == NULL)
                        player_name = "?";
        }

        return vsx_connection_new(&address, option_room, player_name);
}

static struct vsx_worker *
create_worker(struct vsx_connection *connection)
{
        struct vsx_error *error = NULL;

        struct vsx_worker *worker = vsx_worker_new(connection, &error);

        if (worker == NULL) {
                fprintf(stderr, "%s\n", error->message);
                vsx_error_free(error);
        }

        return worker;
}

static void
free_main_data(struct vsx_main_data *main_data)
{
        if (main_data->worker)
                vsx_worker_free(main_data->worker);

        if (main_data->connection)
                vsx_connection_free(main_data->connection);

        for (int i = 0; i < VSX_N_ELEMENTS(main_data->wakeup_fds); i++) {
                if (main_data->wakeup_fds[i] != -1)
                        vsx_close(main_data->wakeup_fds[i]);
        }

        vsx_buffer_destroy(&main_data->log_buffer);
        vsx_buffer_destroy(&main_data->alternate_log_buffer);

        pthread_mutex_destroy(&main_data->mutex);

        vsx_free(main_data);
}

static struct vsx_main_data *
create_main_data(void)
{
        struct vsx_main_data *main_data = vsx_calloc(sizeof *main_data);

        pthread_mutex_init(&main_data->mutex, NULL /* attr */);

        vsx_buffer_init(&main_data->log_buffer);
        vsx_buffer_init(&main_data->alternate_log_buffer);

        if (pipe(main_data->wakeup_fds) == -1) {
                main_data->wakeup_fds[0] = -1;
                main_data->wakeup_fds[1] = -1;
                fprintf(stderr, "pipe failed: %s\n", strerror(errno));
                goto error;
        }

        main_data->connection = create_connection();

        if (main_data->connection == NULL)
                goto error;

        main_data->worker = create_worker(main_data->connection);

        if (main_data->worker == NULL)
                goto error;

        return main_data;

error:
        free_main_data(main_data);
        return NULL;
}

int
main(int argc, char **argv)
{
        if (!process_arguments(argc, argv))
                return EXIT_FAILURE;

        struct vsx_main_data *main_data = create_main_data();

        if (main_data == NULL)
                return EXIT_FAILURE;

        vsx_worker_lock(main_data->worker);

        struct vsx_signal *event_signal =
                vsx_connection_get_event_signal(main_data->connection);

        main_data->event_listener.notify = event_cb;

        vsx_signal_add(event_signal, &main_data->event_listener);

        vsx_connection_set_running(main_data->connection, true);

        print_state_message(main_data);

        vsx_worker_unlock(main_data->worker);

        run_main_loop(main_data);

        free_main_data(main_data);

        return EXIT_SUCCESS;
}
