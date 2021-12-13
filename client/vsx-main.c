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
#include <readline/readline.h>
#include <term.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>
#include <errno.h>
#include <pthread.h>

#include "vsx-connection.h"
#include "vsx-utf8.h"
#include "vsx-netaddress.h"
#include "vsx-monotonic.h"
#include "vsx-buffer.h"
#include "vsx-worker.h"

struct vsx_main_data {
        struct vsx_connection *connection;
        struct vsx_worker *worker;
        bool had_eof;
        bool should_quit;

        bool typing_state;
        bool displayed_typing_state;

        pthread_mutex_t log_mutex;
        struct vsx_buffer log_buffer;
        struct vsx_buffer alternate_log_buffer;
        int log_wakeup_fds[2];
        bool log_wakeup_queued;

        struct vsx_listener event_listener;
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

/* The readline callback doesn’t have a data parameter so this is a
 * messy backdoor to pass the main_data to it.
 */
static struct vsx_main_data *readline_cb_data;

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

static const char typing_prompt[] = "vs*> ";
static const char not_typing_prompt[] = "vs > ";

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
wake_up_log_locked(struct vsx_main_data *main_data)
{
        if (main_data->log_wakeup_queued)
                return;

        static const uint8_t byte = 'W';
        write(main_data->log_wakeup_fds[1], &byte, 1);
        main_data->log_wakeup_queued = true;
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
                pthread_mutex_lock(&main_data->log_mutex);
                wake_up_log_locked(main_data);
                main_data->should_quit = true;
                pthread_mutex_unlock(&main_data->log_mutex);
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
output_ti(char *name)
{
        const char *s;

        s = tigetstr(name);
        if (s && s != (char *) -1)
                fputs(s, stdout);
}

static void
clear_line(void)
{
        output_ti("cr");
        output_ti("dl1");
}

struct check_typing_data {
        const struct vsx_player *self;
        bool is_typing;
};

static void
check_typing_cb(const struct vsx_player *player,
                void *user_data)
{
        struct check_typing_data *data = user_data;

        if (player != data->self && vsx_player_is_typing(player))
                data->is_typing = true;
}

static void
handle_player_changed(struct vsx_main_data *main_data,
                      const struct vsx_connection_event *event)
{
        struct check_typing_data data;

        data.self = vsx_connection_get_self(main_data->connection);
        data.is_typing = false;

        vsx_connection_foreach_player(main_data->connection,
                                      check_typing_cb,
                                      &data);

        pthread_mutex_lock(&main_data->log_mutex);
        main_data->typing_state = data.is_typing;
        wake_up_log_locked(main_data);
        pthread_mutex_unlock(&main_data->log_mutex);
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
        case VSX_CONNECTION_EVENT_TYPE_PLAYER_CHANGED:
                handle_player_changed(main_data, event);
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
        case VSX_CONNECTION_EVENT_TYPE_POLL_CHANGED:
                break;
        }
}

static void
finish_stdin(struct vsx_main_data *main_data)
{
        if (!main_data->had_eof) {
                clear_line();
                rl_callback_handler_remove();
                main_data->had_eof = true;
        }
}

static void
readline_cb(char *line)
{
        struct vsx_main_data *main_data = readline_cb_data;

        if (line == NULL) {
                finish_stdin(main_data);

                if (vsx_connection_get_state(main_data->connection) ==
                    VSX_CONNECTION_STATE_IN_PROGRESS) {
                        vsx_worker_lock(main_data->worker);
                        vsx_connection_leave(main_data->connection);
                        vsx_worker_unlock(main_data->worker);
                } else {
                        pthread_mutex_lock(&main_data->log_mutex);
                        wake_up_log_locked(main_data);
                        main_data->should_quit = true;
                        pthread_mutex_unlock(&main_data->log_mutex);
                }
        }
}

static void
format_print(struct vsx_main_data *main_data,
             const char *format,
             ...)
{
        va_list ap;

        va_start(ap, format);

        pthread_mutex_lock(&main_data->log_mutex);

        vsx_buffer_append_vprintf(&main_data->log_buffer, format, ap);

        wake_up_log_locked(main_data);

        pthread_mutex_unlock(&main_data->log_mutex);

        va_end(ap);
}

static int
newline_cb(int count, int key)
{
        struct vsx_main_data *main_data = readline_cb_data;

        if (*rl_line_buffer) {
                vsx_worker_lock(main_data->worker);

                if (!strcmp(rl_line_buffer, "s")) {
                        vsx_connection_shout(main_data->connection);
                } else if (!strcmp(rl_line_buffer, "t")) {
                        vsx_connection_turn(main_data->connection);
                } else if (!strcmp(rl_line_buffer, "m")) {
                        vsx_connection_move_tile(main_data->connection,
                                                 0,
                                                 10, 20);
                } else {
                        vsx_connection_send_message(main_data->connection,
                                                    rl_line_buffer);
                }

                vsx_worker_unlock(main_data->worker);

                rl_replace_line("", true);
        }

        return 0;
}

static void
redisplay_hook(void)
{
        struct vsx_main_data *main_data = readline_cb_data;

        vsx_worker_lock(main_data->worker);

        /* There doesn't appear to be a good way to hook into
         * notifications of the buffer being modified so we'll just
         * hook into the redisplay function which should hopefully get
         * called every time it is modified. If the buffer is not
         * empty then we'll assume the user is typing. If the user is
         * already marked as typing then this will do nothing */
        vsx_connection_set_typing(main_data->connection,
                                  *rl_line_buffer != '\0');

        vsx_worker_unlock(main_data->worker);

        /* Chain up */
        rl_redisplay();
}

static void
start_stdin(struct vsx_main_data *main_data)
{
        readline_cb_data = main_data;
        rl_callback_handler_install(not_typing_prompt, readline_cb);
        rl_redisplay_function = redisplay_hook;
        rl_bind_key('\r', newline_cb);
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

static bool
handle_log(struct vsx_main_data *main_data)
{
        uint8_t byte;

        int got = read(main_data->log_wakeup_fds[0], &byte, 1);

        if (got == 0) {
                fprintf(stderr, "Unexpected EOF on log wakeup fd\n");
                return false;
        } else if (got == -1) {
                if (errno == EINTR)
                        return true;
                fprintf(stderr,
                        "Error reading log wakeup fd: %s\n",
                        strerror(errno));
                return false;
        }

        pthread_mutex_lock(&main_data->log_mutex);

        main_data->log_wakeup_queued = false;

        struct vsx_buffer tmp = main_data->log_buffer;
        main_data->log_buffer = main_data->alternate_log_buffer;
        main_data->alternate_log_buffer = tmp;

        vsx_buffer_set_length(&main_data->log_buffer, 0);

        bool new_typing_state = main_data->typing_state;

        pthread_mutex_unlock(&main_data->log_mutex);

        clear_line();

        fwrite(main_data->alternate_log_buffer.data,
               1,
               main_data->alternate_log_buffer.length,
               stdout);

        if (!main_data->had_eof) {
                if (new_typing_state != main_data->displayed_typing_state) {
                        main_data->displayed_typing_state = new_typing_state;
                        rl_set_prompt(new_typing_state ?
                                      typing_prompt :
                                      not_typing_prompt);
                }

                rl_forced_update_display();
        }

        return true;
}

static void
run_main_loop(struct vsx_main_data *main_data)
{
        while (!main_data->should_quit) {
                struct pollfd fds[2];
                int nfds = 0;

                fds[nfds].fd = main_data->log_wakeup_fds[0];
                fds[nfds].events = POLLIN;
                fds[nfds].revents = 0;
                nfds++;

                if (!main_data->had_eof) {
                        fds[nfds].fd = STDIN_FILENO;
                        fds[nfds].events = POLLIN;
                        fds[nfds].revents = 0;
                        nfds++;
                }

                if (poll(fds, nfds, -1 /* timeout */) == -1) {
                        fprintf(stderr, "poll failed: %s\n", strerror(errno));
                        break;
                }

                if (!main_data->had_eof && fds[nfds - 1].revents) {
                        rl_callback_read_char();
                        continue;
                }

                if (fds[0].revents && !handle_log(main_data))
                        break;
        }
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

        for (int i = 0; i < VSX_N_ELEMENTS(main_data->log_wakeup_fds); i++) {
                if (main_data->log_wakeup_fds[i] != -1)
                        vsx_close(main_data->log_wakeup_fds[i]);
        }

        vsx_buffer_destroy(&main_data->log_buffer);
        vsx_buffer_destroy(&main_data->alternate_log_buffer);

        pthread_mutex_destroy(&main_data->log_mutex);

        vsx_free(main_data);
}

static struct vsx_main_data *
create_main_data(void)
{
        struct vsx_main_data *main_data = vsx_calloc(sizeof *main_data);

        pthread_mutex_init(&main_data->log_mutex, NULL /* attr */);

        vsx_buffer_init(&main_data->log_buffer);
        vsx_buffer_init(&main_data->alternate_log_buffer);

        if (pipe(main_data->log_wakeup_fds) == -1) {
                main_data->log_wakeup_fds[0] = -1;
                main_data->log_wakeup_fds[1] = -1;
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

        start_stdin(main_data);

        vsx_worker_lock(main_data->worker);

        struct vsx_signal *event_signal =
                vsx_connection_get_event_signal(main_data->connection);

        main_data->event_listener.notify = event_cb;

        vsx_signal_add(event_signal, &main_data->event_listener);

        vsx_connection_set_running(main_data->connection, true);

        print_state_message(main_data);

        vsx_worker_unlock(main_data->worker);

        run_main_loop(main_data);

        finish_stdin(main_data);

        free_main_data(main_data);

        return EXIT_SUCCESS;
}
