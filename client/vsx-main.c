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
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <SDL.h>

#include "vsx-gl.h"
#include "vsx-connection.h"
#include "vsx-utf8.h"
#include "vsx-netaddress.h"
#include "vsx-monotonic.h"
#include "vsx-buffer.h"
#include "vsx-worker.h"
#include "vsx-game-state.h"
#include "vsx-asset-linux.h"

#define MIN_GL_MAJOR_VERSION 2
#define MIN_GL_MINOR_VERSION 0
#define REQUEST_GL_MAJOR_VERSION 2
#define REQUEST_GL_MINOR_VERSION 0
#define VSX_GL_PROFILE SDL_GL_CONTEXT_PROFILE_ES

/* Pick a default resolution that matches the aspect ratio of a Google
 * Pixel 3a in landscape orientation
 */
#define DEFAULT_WIDTH (2220 * 2 / 5)
#define DEFAULT_HEIGHT (1080 * 2 / 5)

struct vsx_main_data {
        SDL_Window *window;
        SDL_GLContext gl_context;

        bool sdl_inited;

        struct vsx_connection *connection;
        struct vsx_worker *worker;
        struct vsx_game_state *game_state;
        struct vsx_asset_manager *asset_manager;

        struct vsx_listener event_listener;

        SDL_Event wakeup_event;

        pthread_mutex_t mutex;

        /* The following state is protected by the mutex */

        bool wakeup_queued;

        bool redraw_queued;

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
handle_event_locked(struct vsx_main_data *main_data,
                    const SDL_Event *event)
{
        switch (event->type) {
        case SDL_WINDOWEVENT:
                switch (event->window.event) {
                case SDL_WINDOWEVENT_CLOSE:
                        main_data->should_quit = true;
                        break;
                case SDL_WINDOWEVENT_EXPOSED:
                        main_data->redraw_queued = true;
                        break;
                }
                goto handled;

        case SDL_QUIT:
                main_data->should_quit = true;
                goto handled;
        }

        if (event->type == main_data->wakeup_event.type) {
                main_data->wakeup_queued = false;
                goto handled;
        }

handled:
        (void) 0;
}

static void
wake_up_locked(struct vsx_main_data *main_data)
{
        if (main_data->wakeup_queued)
                return;

        SDL_PushEvent(&main_data->wakeup_event);
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
paint(struct vsx_main_data *main_data)
{
        vsx_worker_lock(main_data->worker);
        vsx_game_state_update(main_data->game_state);
        vsx_worker_unlock(main_data->worker);

        vsx_gl.glClear(GL_COLOR_BUFFER_BIT);

        SDL_GL_SwapWindow(main_data->window);
}

static void
run_main_loop(struct vsx_main_data *main_data)
{
        pthread_mutex_lock(&main_data->mutex);

        while (!main_data->should_quit) {
                bool redraw_queued = main_data->redraw_queued;

                pthread_mutex_unlock(&main_data->mutex);

                SDL_Event event;
                bool had_event;

                if (redraw_queued)
                        had_event = SDL_PollEvent(&event);
                else
                        had_event = SDL_WaitEvent(&event);

                pthread_mutex_lock(&main_data->mutex);

                if (had_event) {
                        handle_event_locked(main_data, &event);
                } else if (main_data->redraw_queued) {
                        main_data->redraw_queued = false;

                        pthread_mutex_unlock(&main_data->mutex);
                        paint(main_data);
                        pthread_mutex_lock(&main_data->mutex);
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
finish_sdl(struct vsx_main_data *main_data)
{
        if (main_data->gl_context) {
                SDL_GL_MakeCurrent(NULL, NULL);
                SDL_GL_DeleteContext(main_data->gl_context);
        }

        if (main_data->window)
                SDL_DestroyWindow(main_data->window);

        if (main_data->sdl_inited)
                SDL_Quit();
}

static void
free_main_data(struct vsx_main_data *main_data)
{
        finish_sdl(main_data);

        if (main_data->worker)
                vsx_worker_free(main_data->worker);

        if (main_data->game_state)
                vsx_game_state_free(main_data->game_state);

        if (main_data->connection)
                vsx_connection_free(main_data->connection);

        if (main_data->asset_manager)
                vsx_asset_manager_free(main_data->asset_manager);

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

        main_data->asset_manager = vsx_asset_manager_new();

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

static bool
check_gl_version(void)
{
        if (vsx_gl.major_version < 0 ||
            vsx_gl.minor_version < 0) {
                fprintf(stderr,
                        "Invalid GL version string encountered: %s",
                        (const char *)
                        vsx_gl.glGetString(GL_VERSION));

                return false;
        }

        if (vsx_gl.major_version < MIN_GL_MAJOR_VERSION ||
            (vsx_gl.major_version == MIN_GL_MAJOR_VERSION &&
             vsx_gl.minor_version < MIN_GL_MINOR_VERSION)) {
                fprintf(stderr,
                        "GL version %i.%i is required but the driver "
                        "is reporting:\n"
                        "Version: %s\n"
                        "Vendor: %s\n"
                        "Renderer: %s",
                        MIN_GL_MAJOR_VERSION,
                        MIN_GL_MINOR_VERSION,
                        (const char *)
                        vsx_gl.glGetString(GL_VERSION),
                        (const char *)
                        vsx_gl.glGetString(GL_VENDOR),
                        (const char *)
                        vsx_gl.glGetString(GL_RENDERER));

                return false;
        }

        return true;
}

static void *
gl_get_proc_address(const char *func_name,
                    void *unused)
{
        return SDL_GL_GetProcAddress(func_name);
}

static bool
init_sdl(struct vsx_main_data *main_data)
{
        int res = SDL_Init(SDL_INIT_VIDEO);

        if (res < 0) {
                fprintf(stderr, "Unable to init SDL: %s\n", SDL_GetError());
                return false;
        }

        main_data->sdl_inited = true;

        main_data->wakeup_event.type = SDL_RegisterEvents(1);

        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,
                            REQUEST_GL_MAJOR_VERSION);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,
                            REQUEST_GL_MINOR_VERSION);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            VSX_GL_PROFILE);

        Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;

        main_data->window = SDL_CreateWindow("Verda Ŝtelo",
                                             SDL_WINDOWPOS_UNDEFINED,
                                             SDL_WINDOWPOS_UNDEFINED,
                                             DEFAULT_WIDTH,
                                             DEFAULT_HEIGHT,
                                             flags);

        if (main_data->window == NULL) {
                fprintf(stderr,
                        "Failed to create SDL window: %s",
                        SDL_GetError());
                return false;
        }

        main_data->gl_context = SDL_GL_CreateContext(main_data->window);

        if (main_data->gl_context == NULL) {
                fprintf(stderr,
                        "Failed to create GL context: %s",
                        SDL_GetError());
                return false;
        }

        SDL_GL_MakeCurrent(main_data->window, main_data->gl_context);

        vsx_gl_init(gl_get_proc_address, NULL /* user_data */);

        if (!check_gl_version())
                return false;

        return true;
}

int
main(int argc, char **argv)
{
        if (!process_arguments(argc, argv))
                return EXIT_FAILURE;

        struct vsx_main_data *main_data = create_main_data();

        if (main_data == NULL)
                return EXIT_FAILURE;

        int ret = EXIT_SUCCESS;

        if (!init_sdl(main_data)) {
                ret = EXIT_FAILURE;
                goto out;
        }

        vsx_worker_lock(main_data->worker);

        main_data->game_state = vsx_game_state_new(main_data->connection);

        struct vsx_signal *event_signal =
                vsx_connection_get_event_signal(main_data->connection);

        main_data->event_listener.notify = event_cb;

        vsx_signal_add(event_signal, &main_data->event_listener);

        vsx_connection_set_running(main_data->connection, true);

        print_state_message(main_data);

        vsx_worker_unlock(main_data->worker);

        run_main_loop(main_data);

out:
        free_main_data(main_data);

        return ret;
}
