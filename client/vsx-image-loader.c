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

#include "vsx-image-loader.h"

#include <assert.h>

#include "vsx-util.h"
#include "vsx-list.h"
#include "vsx-main-thread.h"
#include "vsx-thread.h"

struct vsx_image_loader_token {
        struct vsx_image_loader *loader;

        bool cancelled;
        char *filename;
        vsx_image_loader_callback func;
        void *user_data;
        struct vsx_image_loader_token *next;
        bool image_loaded;
        struct vsx_image image;
        struct vsx_error *error;
};

struct vsx_image_loader {
        pthread_mutex_t mutex;
        pthread_cond_t cond;
        pthread_t thread;
        struct vsx_main_thread *main_thread;
        struct vsx_asset_manager *asset_manager;
        bool quit;

        struct vsx_image_loader_token *queue;
        struct vsx_image_loader_token *notify_slot;
        struct vsx_main_thread_token *idle_token;
};

static void
free_token(struct vsx_image_loader_token *token)
{
        if (token->image_loaded)
                vsx_image_destroy(&token->image);

        if (token->error)
                vsx_error_free(token->error);

        vsx_free(token->filename);

        vsx_free(token);
}

static void
handle_token(struct vsx_image_loader *loader,
             struct vsx_image_loader_token *token)
{
        struct vsx_asset *asset =
                vsx_asset_manager_open(loader->asset_manager,
                                       token->filename,
                                       &token->error);

        if (asset == NULL)
                return;

        size_t asset_size;
        bool ret = false;

        if (vsx_asset_remaining(asset, &asset_size, &token->error)) {
                ret = vsx_image_load_asset_with_size(&token->image,
                                                     asset,
                                                     asset_size,
                                                     &token->error);
        }

        vsx_asset_close(asset);

        if (ret)
                token->image_loaded = true;
}

static void
idle_cb(void *user_data)
{
        struct vsx_image_loader *loader = user_data;

        pthread_mutex_lock(&loader->mutex);

        struct vsx_image_loader_token *token = loader->notify_slot;

        assert(token);

        loader->notify_slot = NULL;
        loader->idle_token = NULL;

        pthread_cond_signal(&loader->cond);

        pthread_mutex_unlock(&loader->mutex);

        if (!token->cancelled) {
                token->func(token->image_loaded ?
                            &token->image :
                            NULL,
                            token->error,
                            token->user_data);
        }

        free_token(token);
}

static void *
thread_func(void *user_data)
{
        struct vsx_image_loader *loader = user_data;

        pthread_mutex_lock(&loader->mutex);

        while (true) {
                if (!loader->quit &&
                    (loader->queue == NULL || loader->notify_slot != NULL))
                        pthread_cond_wait(&loader->cond, &loader->mutex);

                if (loader->quit)
                        break;

                if (loader->queue != NULL && loader->notify_slot == NULL) {
                        struct vsx_image_loader_token *token = loader->queue;

                        loader->queue = token->next;

                        if (token->cancelled) {
                                free_token(token);
                        } else {
                                pthread_mutex_unlock(&loader->mutex);
                                handle_token(loader, token);
                                pthread_mutex_lock(&loader->mutex);

                                assert(loader->notify_slot == NULL);
                                loader->notify_slot = token;

                                assert(loader->idle_token == NULL);

                                struct vsx_main_thread *mt =
                                        loader->main_thread;

                                loader->idle_token =
                                        vsx_main_thread_queue_idle(mt,
                                                                   idle_cb,
                                                                   loader);
                        }
                }
        }

        pthread_mutex_unlock(&loader->mutex);

        return NULL;
}

struct vsx_image_loader *
vsx_image_loader_new(struct vsx_main_thread *main_thread,
                     struct vsx_asset_manager *asset_manager)
{
        struct vsx_image_loader *loader = vsx_calloc(sizeof *loader);

        loader->main_thread = main_thread;
        loader->asset_manager = asset_manager;

        pthread_mutex_init(&loader->mutex, NULL);
        pthread_cond_init(&loader->cond, NULL);

        vsx_thread_create(&loader->thread, NULL, thread_func, loader);

        return loader;
}

struct vsx_image_loader_token *
vsx_image_loader_load(struct vsx_image_loader *loader,
                      const char *filename,
                      vsx_image_loader_callback func,
                      void *user_data)
{
        struct vsx_image_loader_token *token =
                vsx_calloc(sizeof *token);

        token->loader = loader;
        token->filename = vsx_strdup(filename);
        token->func = func;
        token->user_data = user_data;

        pthread_mutex_lock(&loader->mutex);

        token->next = loader->queue;
        loader->queue = token;

        pthread_cond_signal(&loader->cond);

        pthread_mutex_unlock(&loader->mutex);

        return token;
}

void
vsx_image_loader_cancel(struct vsx_image_loader_token *token)
{
        pthread_mutex_lock(&token->loader->mutex);

        token->cancelled = true;

        pthread_mutex_unlock(&token->loader->mutex);
}

static void
free_queue(struct vsx_image_loader_token *queue)
{
        struct vsx_image_loader_token *next;

        for (struct vsx_image_loader_token *token = queue;
             token;
             token = next) {
                next = token->next;
                free_token(token);
        }
}

void
vsx_image_loader_free(struct vsx_image_loader *loader)
{
        pthread_mutex_lock(&loader->mutex);
        loader->quit = true;
        pthread_cond_signal(&loader->cond);
        pthread_mutex_unlock(&loader->mutex);

        pthread_join(loader->thread, NULL);

        pthread_mutex_destroy(&loader->mutex);
        pthread_cond_destroy(&loader->cond);

        free_queue(loader->queue);

        if (loader->idle_token)
                vsx_main_thread_cancel_idle(loader->idle_token);

        if (loader->notify_slot)
                free_token(loader->notify_slot);

        vsx_free(loader);
}
