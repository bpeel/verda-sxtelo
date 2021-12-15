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

#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <SDL.h>

#include "vsx-asset-linux.h"
#include "vsx-main-thread.h"
#include "vsx-image-loader.h"

struct data {
        SDL_Event wakeup_event;
        struct vsx_image_loader *loader;
        struct vsx_asset_manager *asset_manager;
};

static void
run_main_loop_until_finished(struct data *data,
                             bool *finished)
{
        while (!*finished) {
                SDL_Event event;

                int res = SDL_WaitEventTimeout(&event, 10000);

                assert(res);

                if (event.type == data->wakeup_event.type)
                        vsx_main_thread_flush_idle_events();
        }
}

struct test_load_tiles_data {
        bool finished;
        pthread_t loading_thread;
};

static void
test_load_tiles_cb(const struct vsx_image *image,
                   struct vsx_error *error,
                   void *user_data)
{
        struct test_load_tiles_data *data = user_data;

        assert(data);
        assert(!data->finished);
        assert(image != NULL);
        assert(error == NULL);
        assert(image->data);
        assert(image->width > 0);
        assert(image->height > 0);
        assert(image->components == 3);
        assert(pthread_self() == data->loading_thread);

        data->finished = true;
}

static void
test_load_tiles(struct data *data)
{
        struct test_load_tiles_data tiles_data = {
                .finished = false,
                .loading_thread = pthread_self(),
        };

        struct vsx_image_loader_token *token =
                vsx_image_loader_load(data->loader,
                                      "tiles.mpng",
                                      test_load_tiles_cb,
                                      &tiles_data);

        assert(token);

        run_main_loop_until_finished(data, &tiles_data.finished);
}

struct test_load_multiple_data {
        bool finished;
        int count;
        pthread_t loading_thread;
};

static void
test_load_multiple_cb(const struct vsx_image *image,
                      struct vsx_error *error,
                      void *user_data)
{
        struct test_load_multiple_data *data = user_data;

        assert(data);
        assert(!data->finished);
        assert(image != NULL);
        assert(error == NULL);
        assert(image->data);
        assert(image->width > 0);
        assert(image->height > 0);
        assert(image->components == 3);
        assert(pthread_self() == data->loading_thread);
        assert(data->count < 3);

        if (++data->count >= 3)
                data->finished = true;
}

static void
test_load_multiple(struct data *data)
{
        struct test_load_multiple_data multiple_data = {
                .finished = false,
                .loading_thread = pthread_self(),
        };

        for (int i = 0; i < 3; i++) {
                struct vsx_image_loader_token *token =
                        vsx_image_loader_load(data->loader,
                                              "tiles.mpng",
                                              test_load_multiple_cb,
                                              &multiple_data);

                assert(token);
        }

        run_main_loop_until_finished(data, &multiple_data.finished);
}

struct test_error_data {
        bool finished;
        pthread_t loading_thread;
};

static void
test_error_cb(const struct vsx_image *image,
              struct vsx_error *error,
              void *user_data)
{
        struct test_error_data *data = user_data;

        assert(data);
        assert(!data->finished);
        assert(image == NULL);
        assert(error != NULL);
        assert(error->domain == &vsx_asset_error);
        assert(error->code == VSX_ASSET_ERROR_FILE);
        assert(error->message);
        assert(pthread_self() == data->loading_thread);

        data->finished = true;
}

static void
test_error(struct data *data)
{
        struct test_error_data error_data = {
                .finished = false,
                .loading_thread = pthread_self(),
        };

        struct vsx_image_loader_token *token =
                vsx_image_loader_load(data->loader,
                                      "file-doesnt-exist.png",
                                      test_error_cb,
                                      &error_data);

        assert(token);

        run_main_loop_until_finished(data, &error_data.finished);
}

static void
test_cancel_cb(const struct vsx_image *image,
               struct vsx_error *error,
               void *user_data)
{
        assert(!"This shouldn’t be reached because the load was cancelled");
}

static void
test_cancel(struct data *data,
            bool immediate)
{
        struct vsx_image_loader_token *token =
                vsx_image_loader_load(data->loader,
                                      "tiles.mpng",
                                      test_cancel_cb,
                                      NULL);

        assert(token);

        if (!immediate) {
                /* Leave enough time for the image to actually load
                 * but still cancel it before the idle callback is
                 * invoked.
                 */
                sleep(3);
        }

        vsx_image_loader_cancel(token);

        while (true) {
                SDL_Event event;

                int before = SDL_GetTicks();
                int res = SDL_WaitEventTimeout(&event, 3000);
                int after = SDL_GetTicks();

                if (res) {
                        if (immediate)
                                assert(event.type != data->wakeup_event.type);
                        else if (event.type == data->wakeup_event.type)
                                vsx_main_thread_flush_idle_events();
                } else {
                        /* Make sure that we really waited for 3 seconds. */
                        assert(after - before > 2999);
                        break;
                }
        }
}

static void
test_free_while_loading_cb(const struct vsx_image *image,
                           struct vsx_error *error,
                           void *user_data)
{
        assert(!("This shouldn’t be reached because "
                 "the image loader was freed"));
}

static void
test_free_while_loading(struct data *data)
{
        struct vsx_image_loader *loader =
                vsx_image_loader_new(data->asset_manager);

        assert(loader);

        struct vsx_image_loader_token *token =
                vsx_image_loader_load(loader,
                                      "tiles.mpng",
                                      test_free_while_loading_cb,
                                      NULL);

        assert(token);

        /* Leave enough time for the image to actually load
         * but still free the loader before the idle callback
         * is invoked.
         */
        sleep(3);

        vsx_image_loader_cancel(token);

        vsx_image_loader_free(loader);

        while (true) {
                SDL_Event event;

                int before = SDL_GetTicks();
                int res = SDL_WaitEventTimeout(&event, 3000);
                int after = SDL_GetTicks();

                if (res) {
                        if (event.type == data->wakeup_event.type)
                                vsx_main_thread_flush_idle_events();
                } else {
                        /* Make sure that we really waited for 3 seconds. */
                        assert(after - before > 2999);
                        break;
                }
        }
}

static void
wakeup_cb(void *user_data)
{
        struct data *data = user_data;

        SDL_PushEvent(&data->wakeup_event);
}

int
main(int argc, char **argv)
{
        int res;

        res = chdir(VSX_SOURCE_ROOT);
        assert(res == 0);

        res = SDL_Init(0);
        assert(res >= 0);

        struct data data = {
                .wakeup_event.type = SDL_RegisterEvents(1),
        };

        vsx_main_thread_set_wakeup_func(wakeup_cb, &data);

        data.asset_manager = vsx_asset_manager_new();

        assert(data.asset_manager != NULL);

        data.loader = vsx_image_loader_new(data.asset_manager);

        test_load_tiles(&data);
        test_load_multiple(&data);
        test_error(&data);
        test_cancel(&data, true /* immediate */);
        test_cancel(&data, false /* immediate */);

        vsx_image_loader_free(data.loader);

        test_free_while_loading(&data);

        vsx_asset_manager_free(data.asset_manager);

        vsx_main_thread_clean_up();

        SDL_Quit();

        return EXIT_SUCCESS;
}
