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

#include "vsx-main-thread.h"

#include <pthread.h>
#include <stdbool.h>

#include "vsx-util.h"

struct vsx_main_thread_token {
        bool cancelled;
        vsx_main_thread_idle_func func;
        void *user_data;
        struct vsx_main_thread_token *next;
};

struct data {
        vsx_main_thread_wakeup_func wakeup_func;
        void *wakeup_user_data;
        pthread_mutex_t mutex;
        struct vsx_main_thread_token *freed_tokens;
        struct vsx_main_thread_token *queue;
};

static struct data
data = {
        .mutex = PTHREAD_MUTEX_INITIALIZER,
};

void
vsx_main_thread_set_wakeup_func(vsx_main_thread_wakeup_func func,
                                void *user_data)
{
        data.wakeup_func = func;
        data.wakeup_user_data = user_data;
}

struct vsx_main_thread_token *
vsx_main_thread_queue_idle(vsx_main_thread_idle_func func,
                           void *user_data)
{
        pthread_mutex_lock(&data.mutex);

        struct vsx_main_thread_token *token;

        if (data.freed_tokens == NULL) {
                token = vsx_alloc(sizeof *token);
        } else {
                token = data.freed_tokens;
                data.freed_tokens = token->next;
        }

        token->func = func;
        token->user_data = user_data;
        token->cancelled = false;

        if (data.queue == NULL && data.wakeup_func)
                data.wakeup_func(data.wakeup_user_data);

        token->next = data.queue;
        data.queue = token;

        pthread_mutex_unlock(&data.mutex);

        return token;
}

void
vsx_main_thread_cancel_idle(struct vsx_main_thread_token *token)
{
        pthread_mutex_lock(&data.mutex);
        token->cancelled = true;
        pthread_mutex_unlock(&data.mutex);
}

void
vsx_main_thread_flush_idle_events(void)
{
        pthread_mutex_lock(&data.mutex);

        struct vsx_main_thread_token *queue = data.queue;
        data.queue = NULL;

        pthread_mutex_unlock(&data.mutex);

        for (struct vsx_main_thread_token *token = queue;
             token;
             token = token->next) {
                if (token->cancelled)
                        continue;

                token->func(token->user_data);
        }

        pthread_mutex_lock(&data.mutex);

        if (queue) {
                struct vsx_main_thread_token *last = queue;

                while (last->next)
                        last = last->next;

                last->next = data.freed_tokens;
                data.freed_tokens = queue;
        }

        pthread_mutex_unlock(&data.mutex);
}

static void
free_tokens(struct vsx_main_thread_token *list)
{
        struct vsx_main_thread_token *next;

        for (struct vsx_main_thread_token *token = list; token; token = next) {
                next = token->next;
                vsx_free(token);
        }
}

void
vsx_main_thread_clean_up(void)
{
        free_tokens(data.queue);
        data.queue = NULL;
        free_tokens(data.freed_tokens);
        data.freed_tokens = NULL;
}
