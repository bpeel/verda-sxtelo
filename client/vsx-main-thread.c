/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2021, 2022  Neil Roberts
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
#include <stdint.h>

#include "vsx-util.h"
#include "vsx-monotonic.h"
#include "vsx-thread.h"

struct vsx_main_thread_token {
        bool cancelled;
        vsx_main_thread_idle_func func;
        void *user_data;
        struct vsx_main_thread_token *next;

        /* Time that the idle shout be invoked. This is ignored if the
         * token is not in the timeout queue.
         */
        int64_t wakeup_time;
};

struct data {
        vsx_main_thread_wakeup_func wakeup_func;
        void *wakeup_user_data;
        pthread_mutex_t mutex;
        struct vsx_main_thread_token *freed_tokens;
        struct vsx_main_thread_token *queue;

        /* Queue of timeouts in order of wakeup_time */
        struct vsx_main_thread_token *timeout_queue;

        bool have_timeout_thread;
        pthread_t timeout_thread;
        bool timeout_thread_should_quit;
        bool have_cond;
        pthread_cond_t cond;
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

static void
flush_ready_timeout_events_locked()
{
        if (data.timeout_queue == NULL)
                return;

        int64_t now = vsx_monotonic_get();

        bool found_something = false;

        while (true) {
                struct vsx_main_thread_token *token = data.timeout_queue;

                if (token == NULL || token->wakeup_time > now)
                        break;

                /* Move the token into the idle queue */
                data.timeout_queue = token->next;

                if (token->cancelled) {
                        token->next = data.freed_tokens;
                        data.freed_tokens = token;
                } else {
                        token->next = data.queue;
                        data.queue = token;
                        found_something = true;
                }
        }

        if (found_something && data.wakeup_func)
                data.wakeup_func(data.wakeup_user_data);
}

static void *
timeout_thread_func(void *user_data)
{
        pthread_mutex_lock(&data.mutex);

        while (!data.timeout_thread_should_quit) {
                if (data.timeout_queue == NULL) {
                        /* Wait forever */
                        pthread_cond_wait(&data.cond, &data.mutex);
                } else {
                        int64_t wakeup_time = data.timeout_queue->wakeup_time;

                        struct timespec wait_timespec = {
                                .tv_sec = wakeup_time / 1000000,
                                .tv_nsec = wakeup_time % 1000000 * 1000,
                        };

                        pthread_cond_timedwait(&data.cond,
                                               &data.mutex,
                                               &wait_timespec);


                }

                if (data.timeout_thread_should_quit)
                        break;

                flush_ready_timeout_events_locked();
        }

        pthread_mutex_unlock(&data.mutex);

        return NULL;
}

static struct vsx_main_thread_token *
create_token_locked(vsx_main_thread_idle_func func,
                    void *user_data)
{
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

        return token;
}

static bool
create_cond_locked(void)
{
        pthread_condattr_t attr;

        if (data.have_cond)
                return true;

        if (pthread_condattr_init(&attr))
                return false;

        if (pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) == 0 &&
            pthread_cond_init(&data.cond, &attr) == 0)
                data.have_cond = true;

        pthread_condattr_destroy(&attr);

        return data.have_cond;
}

static bool
create_thread_locked(void)
{
        if (data.have_timeout_thread)
                return true;

        if (!create_cond_locked())
                return false;

        data.timeout_thread_should_quit = false;

        if (vsx_thread_create(&data.timeout_thread,
                              NULL, /* attr */
                              timeout_thread_func,
                              NULL /* arg */) == 0) {
                data.have_timeout_thread = true;
                return true;
        } else {
                return false;
        }
}

struct vsx_main_thread_token *
vsx_main_thread_queue_timeout(uint32_t microseconds,
                              vsx_main_thread_idle_func func,
                              void *user_data)
{
        pthread_mutex_lock(&data.mutex);

        struct vsx_main_thread_token *token =
                create_token_locked(func, user_data);

        token->wakeup_time = vsx_monotonic_get() + microseconds;

        struct vsx_main_thread_token **prev = &data.timeout_queue;

        while (*prev && (*prev)->wakeup_time < token->wakeup_time)
                prev = &(*prev)->next;

        token->next = *prev;
        *prev = token;


        if (create_thread_locked())
                pthread_cond_signal(&data.cond);

        pthread_mutex_unlock(&data.mutex);

        return token;
}

struct vsx_main_thread_token *
vsx_main_thread_queue_idle(vsx_main_thread_idle_func func,
                           void *user_data)
{
        pthread_mutex_lock(&data.mutex);

        struct vsx_main_thread_token *token =
                create_token_locked(func, user_data);

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
        if (data.have_timeout_thread) {
                pthread_mutex_lock(&data.mutex);
                data.timeout_thread_should_quit = true;
                pthread_cond_signal(&data.cond);
                pthread_mutex_unlock(&data.mutex);

                pthread_join(data.timeout_thread, NULL /* retval */);

                data.have_timeout_thread = false;
        }

        if (data.have_cond) {
                pthread_cond_destroy(&data.cond);
                data.have_cond = false;
        }

        free_tokens(data.queue);
        data.queue = NULL;
        free_tokens(data.freed_tokens);
        data.freed_tokens = NULL;
        free_tokens(data.timeout_queue);
        data.timeout_queue = NULL;
}
