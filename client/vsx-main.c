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
#include <errno.h>
#include <string.h>
#include <SDL.h>

#include "vsx-gl.h"
#include "vsx-connection.h"
#include "vsx-utf8.h"
#include "vsx-monotonic.h"
#include "vsx-buffer.h"
#include "vsx-worker.h"
#include "vsx-game-state.h"
#include "vsx-asset-linux.h"
#include "vsx-game-painter.h"
#include "vsx-main-thread.h"

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

/* Although it is possible to ask SDL for a DPI, it doesn’t seem to
 * actually correspond to the DPI of the screen, so we might as well
 * just pick a value that would look similar to how it looks on the
 * phone. 480 is the approximate DPI used on a Google Pixel 3a.
 */
#define DPI (480 * 2 / 5)

struct vsx_main_data {
        SDL_Window *window;
        SDL_GLContext gl_context;

        bool sdl_inited;

        struct vsx_connection *connection;
        struct vsx_worker *worker;
        struct vsx_asset_manager *asset_manager;

        struct vsx_game_state *game_state;
        struct vsx_listener modified_listener;

        bool button_pressed;
        int mouse_x, mouse_y;
        Uint32 button_pressed_device;

        struct vsx_game_painter *game_painter;
        struct vsx_listener redraw_needed_listener;

        SDL_Event wakeup_event;

        bool redraw_queued;

        bool should_quit;
};

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
update_fb_size(struct vsx_main_data *main_data,
               int fb_width,
               int fb_height)
{
        vsx_game_painter_set_fb_size(main_data->game_painter,
                                     fb_width,
                                     fb_height);
}

static void
handle_mouse_wheel(struct vsx_main_data *main_data,
                   const SDL_MouseWheelEvent *event)
{
        if (event->y == 0 || main_data->button_pressed)
                return;

        vsx_game_painter_press_finger(main_data->game_painter,
                                      0, /* finger */
                                      main_data->mouse_x - 100,
                                      main_data->mouse_y);
        vsx_game_painter_press_finger(main_data->game_painter,
                                      1, /* finger */
                                      main_data->mouse_x + 100,
                                      main_data->mouse_y);

        int move_amount = 100 + event->y * 6;

        vsx_game_painter_move_finger(main_data->game_painter,
                                     0,
                                     main_data->mouse_x - move_amount,
                                     main_data->mouse_y);
        vsx_game_painter_move_finger(main_data->game_painter,
                                     1,
                                     main_data->mouse_x + move_amount,
                                     main_data->mouse_y);

        vsx_game_painter_release_finger(main_data->game_painter, 0);
        vsx_game_painter_release_finger(main_data->game_painter, 1);
}

static void
handle_mouse_button(struct vsx_main_data *main_data,
                    const SDL_MouseButtonEvent *event)
{
        if (event->button != 1)
                return;

        if (event->state == SDL_PRESSED) {
                if (main_data->button_pressed)
                        return;

                main_data->button_pressed = true;
                main_data->button_pressed_device = event->which;

                vsx_game_painter_press_finger(main_data->game_painter,
                                              0, /* finger */
                                              event->x,
                                              event->y);
        } else {
                if (!main_data->button_pressed ||
                    main_data->button_pressed_device != event->which)
                        return;

                main_data->button_pressed = false;
                vsx_game_painter_release_finger(main_data->game_painter,
                                                0 /* finger */);
        }
}

static void
handle_mouse_motion(struct vsx_main_data *main_data,
                    const SDL_MouseMotionEvent *event)
{
        main_data->mouse_x = event->x;
        main_data->mouse_y = event->y;

        if (!main_data->button_pressed ||
            event->which != main_data->button_pressed_device)
                return;

        vsx_game_painter_move_finger(main_data->game_painter,
                                     0, /* finger */
                                     event->x,
                                     event->y);
}

static void
handle_event(struct vsx_main_data *main_data,
             const SDL_Event *event)
{
        int fb_width, fb_height;

        switch (event->type) {
        case SDL_WINDOWEVENT:
                switch (event->window.event) {
                case SDL_WINDOWEVENT_CLOSE:
                        main_data->should_quit = true;
                        break;
                case SDL_WINDOWEVENT_SHOWN:
                        SDL_GetWindowSize(main_data->window,
                                          &fb_width,
                                          &fb_height);
                        update_fb_size(main_data, fb_width, fb_height);
                        break;
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                        update_fb_size(main_data,
                                       event->window.data1,
                                       event->window.data2);
                        main_data->redraw_queued = true;
                        break;
                case SDL_WINDOWEVENT_EXPOSED:
                        main_data->redraw_queued = true;
                        break;
                }
                goto handled;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
                handle_mouse_button(main_data, &event->button);
                goto handled;

        case SDL_MOUSEWHEEL:
                handle_mouse_wheel(main_data, &event->wheel);
                goto handled;

        case SDL_MOUSEMOTION:
                handle_mouse_motion(main_data, &event->motion);
                goto handled;

        case SDL_QUIT:
                main_data->should_quit = true;
                goto handled;
        }

        if (event->type == main_data->wakeup_event.type) {
                vsx_main_thread_flush_idle_events();
                goto handled;
        }

handled:
        (void) 0;
}

static void
wakeup_cb(void *user_data)
{
        struct vsx_main_data *main_data = user_data;

        SDL_PushEvent(&main_data->wakeup_event);
}

static void
paint(struct vsx_main_data *main_data)
{
        vsx_game_state_update(main_data->game_state);

        vsx_game_painter_paint(main_data->game_painter);

        SDL_GL_SwapWindow(main_data->window);
}

static void
run_main_loop(struct vsx_main_data *main_data)
{
        while (!main_data->should_quit) {
                bool redraw_queued = main_data->redraw_queued;

                SDL_Event event;
                bool had_event;

                if (redraw_queued)
                        had_event = SDL_PollEvent(&event);
                else
                        had_event = SDL_WaitEvent(&event);

                if (had_event) {
                        handle_event(main_data, &event);
                } else if (main_data->redraw_queued) {
                        main_data->redraw_queued = false;

                        paint(main_data);
                }
        }
}

static struct vsx_connection *
create_connection(void)
{
        const char *player_name = option_player_name;

        if (player_name == NULL) {
                player_name = getlogin();
                if (player_name == NULL)
                        player_name = "?";
        }

        return vsx_connection_new(option_room, player_name);
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

        vsx_worker_queue_address_resolve(worker,
                                         option_server,
                                         option_server_port);

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
        if (main_data->game_painter)
                vsx_game_painter_free(main_data->game_painter);

        finish_sdl(main_data);

        if (main_data->worker)
                vsx_worker_free(main_data->worker);

        if (main_data->game_state)
                vsx_game_state_free(main_data->game_state);

        if (main_data->connection)
                vsx_connection_free(main_data->connection);

        if (main_data->asset_manager)
                vsx_asset_manager_free(main_data->asset_manager);

        vsx_free(main_data);
}

static struct vsx_main_data *
create_main_data(void)
{
        struct vsx_main_data *main_data = vsx_calloc(sizeof *main_data);

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
        vsx_main_thread_set_wakeup_func(wakeup_cb, main_data);

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

static void
game_state_modified_cb(struct vsx_listener *listener,
                       void *user_data)
{
        struct vsx_main_data *main_data =
                vsx_container_of(listener,
                                 struct vsx_main_data,
                                 modified_listener);

        main_data->redraw_queued = true;
}

static void
init_game_state(struct vsx_main_data *main_data)
{
        main_data->game_state = vsx_game_state_new(main_data->worker,
                                                   main_data->connection);

        struct vsx_signal *modified_signal =
                vsx_game_state_get_modified_signal(main_data->game_state);

        main_data->modified_listener.notify = game_state_modified_cb;
        vsx_signal_add(modified_signal, &main_data->modified_listener);
}

static void
redraw_needed_cb(struct vsx_listener *listener,
                 void *signal_data)
{
        struct vsx_main_data *main_data =
                vsx_container_of(listener,
                                 struct vsx_main_data,
                                 redraw_needed_listener);

        main_data->redraw_queued = true;
}

static bool
init_painter(struct vsx_main_data *main_data)
{
        struct vsx_error *error = NULL;

        struct vsx_game_painter *game_painter =
                vsx_game_painter_new(main_data->game_state,
                                     main_data->asset_manager,
                                     DPI,
                                     &error);

        if (game_painter == NULL) {
                fprintf(stderr, "%s\n", error->message);
                vsx_error_free(error);
                return false;
        }

        main_data->game_painter = game_painter;

        main_data->redraw_needed_listener.notify = redraw_needed_cb;

        struct vsx_signal *redraw_needed_signal =
                vsx_game_painter_get_redraw_needed_signal(game_painter);
        vsx_signal_add(redraw_needed_signal,
                       &main_data->redraw_needed_listener);

        return true;
}

int
main(int argc, char **argv)
{
        if (!process_arguments(argc, argv))
                return EXIT_FAILURE;

        SDL_SetHint(SDL_HINT_VIDEO_X11_FORCE_EGL, "1");

        struct vsx_main_data *main_data = create_main_data();

        if (main_data == NULL)
                return EXIT_FAILURE;

        int ret = EXIT_SUCCESS;

        if (!init_sdl(main_data)) {
                ret = EXIT_FAILURE;
                goto out;
        }

        init_game_state(main_data);

        if (!init_painter(main_data)) {
                ret = EXIT_FAILURE;
                goto out;
        }

        vsx_worker_lock(main_data->worker);

        vsx_connection_set_running(main_data->connection, true);

        vsx_worker_unlock(main_data->worker);

        run_main_loop(main_data);

out:
        free_main_data(main_data);

        vsx_main_thread_clean_up();

        return ret;
}
