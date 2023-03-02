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
        struct vsx_main_thread *mt;

        bool cancelled;
        vsx_main_thread_idle_func func;
        void *user_data;
        struct vsx_main_thread_token *next;

        /* Time that the idle shout be invoked. This is ignored if the
         * token is not in the timeout queue.
         */
        int64_t wakeup_time;
};

struct vsx_main_thread {
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

struct vsx_main_thread *
vsx_main_thread_new(vsx_main_thread_wakeup_func func,
                    void *user_data)
{
        struct vsx_main_thread *mt = vsx_calloc(sizeof *mt);

        pthread_mutex_init(&mt->mutex, NULL);

        mt->wakeup_func = func;
        mt->wakeup_user_data = user_data;

        return mt;
}

static void
flush_ready_timeout_events_locked(struct vsx_main_thread *mt)
{
        if (mt->timeout_queue == NULL)
                return;

        int64_t now = vsx_monotonic_get();

        bool found_something = false;

        while (true) {
                struct vsx_main_thread_token *token = mt->timeout_queue;

                if (token == NULL || token->wakeup_time > now)
                        break;

                /* Move the token into the idle queue */
                mt->timeout_queue = token->next;

                if (token->cancelled) {
                        token->next = mt->freed_tokens;
                        mt->freed_tokens = token;
                } else {
                        token->next = mt->queue;
                        mt->queue = token;
                        found_something = true;
                }
        }

        if (found_something && mt->wakeup_func)
                mt->wakeup_func(mt->wakeup_user_data);
}

static void
wait_until_timeout(struct vsx_main_thread *mt, int64_t wakeup_time)
{
#ifdef __APPLE__
    
    int64_t sleep_time = wakeup_time - vsx_monotonic_get();
    
    if (sleep_time <= 0)
        return;

    struct timespec wait_timespec = {
            .tv_sec = sleep_time / 1000000,
            .tv_nsec = sleep_time % 1000000 * 1000,
    };
    
    pthread_cond_timedwait_relative_np(&mt->cond,
                                       &mt->mutex,
                                       &wait_timespec);

#else

    struct timespec wait_timespec = {
            .tv_sec = wakeup_time / 1000000,
            .tv_nsec = wakeup_time % 1000000 * 1000,
    };

    pthread_cond_timedwait(&mt->cond,
                           &mt->mutex,
                           &wait_timespec);

#endif
}

static void *
timeout_thread_func(void *user_data)
{
        struct vsx_main_thread *mt = user_data;

        pthread_mutex_lock(&mt->mutex);

        while (!mt->timeout_thread_should_quit) {
                if (mt->timeout_queue == NULL) {
                        /* Wait forever */
                        pthread_cond_wait(&mt->cond, &mt->mutex);
                } else {
                        wait_until_timeout(mt, mt->timeout_queue->wakeup_time);
                }

                if (mt->timeout_thread_should_quit)
                        break;

                flush_ready_timeout_events_locked(mt);
        }

        pthread_mutex_unlock(&mt->mutex);

        return NULL;
}

static struct vsx_main_thread_token *
create_token_locked(struct vsx_main_thread *mt,
                    vsx_main_thread_idle_func func,
                    void *user_data)
{
        struct vsx_main_thread_token *token;

        if (mt->freed_tokens == NULL) {
                token = vsx_alloc(sizeof *token);
        } else {
                token = mt->freed_tokens;
                mt->freed_tokens = token->next;
        }

        token->mt = mt;
        token->func = func;
        token->user_data = user_data;
        token->cancelled = false;

        return token;
}

static bool
create_cond_locked(struct vsx_main_thread *mt)
{
        pthread_condattr_t attr;

        if (mt->have_cond)
                return true;

        if (pthread_condattr_init(&attr))
                return false;

        if (
#ifndef __APPLE__
            pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) == 0 &&
#endif
            pthread_cond_init(&mt->cond, &attr) == 0)
                mt->have_cond = true;

        pthread_condattr_destroy(&attr);

        return mt->have_cond;
}

static bool
create_thread_locked(struct vsx_main_thread *mt)
{
        if (mt->have_timeout_thread)
                return true;

        if (!create_cond_locked(mt))
                return false;

        mt->timeout_thread_should_quit = false;

        if (vsx_thread_create(&mt->timeout_thread,
                              "TimeoutSleeper",
                              NULL, /* attr */
                              timeout_thread_func,
                              mt) == 0) {
                mt->have_timeout_thread = true;
                return true;
        } else {
                return false;
        }
}

struct vsx_main_thread_token *
vsx_main_thread_queue_timeout(struct vsx_main_thread *mt,
                              uint32_t microseconds,
                              vsx_main_thread_idle_func func,
                              void *user_data)
{
        pthread_mutex_lock(&mt->mutex);

        struct vsx_main_thread_token *token =
                create_token_locked(mt, func, user_data);

        token->wakeup_time = vsx_monotonic_get() + microseconds;

        struct vsx_main_thread_token **prev = &mt->timeout_queue;

        while (*prev && (*prev)->wakeup_time < token->wakeup_time)
                prev = &(*prev)->next;

        token->next = *prev;
        *prev = token;


        if (create_thread_locked(mt))
                pthread_cond_signal(&mt->cond);

        pthread_mutex_unlock(&mt->mutex);

        return token;
}

struct vsx_main_thread_token *
vsx_main_thread_queue_idle(struct vsx_main_thread *mt,
                           vsx_main_thread_idle_func func,
                           void *user_data)
{
        pthread_mutex_lock(&mt->mutex);

        struct vsx_main_thread_token *token =
                create_token_locked(mt, func, user_data);

        if (mt->queue == NULL && mt->wakeup_func)
                mt->wakeup_func(mt->wakeup_user_data);

        token->next = mt->queue;
        mt->queue = token;

        pthread_mutex_unlock(&mt->mutex);

        return token;
}

void
vsx_main_thread_cancel_idle(struct vsx_main_thread_token *token)
{
        pthread_mutex_lock(&token->mt->mutex);
        token->cancelled = true;
        pthread_mutex_unlock(&token->mt->mutex);
}

void
vsx_main_thread_flush_idle_events(struct vsx_main_thread *mt)
{
        pthread_mutex_lock(&mt->mutex);

        struct vsx_main_thread_token *queue = mt->queue;
        mt->queue = NULL;

        pthread_mutex_unlock(&mt->mutex);

        for (struct vsx_main_thread_token *token = queue;
             token;
             token = token->next) {
                if (token->cancelled)
                        continue;

                token->func(token->user_data);
        }

        pthread_mutex_lock(&mt->mutex);

        if (queue) {
                struct vsx_main_thread_token *last = queue;

                while (last->next)
                        last = last->next;

                last->next = mt->freed_tokens;
                mt->freed_tokens = queue;
        }

        pthread_mutex_unlock(&mt->mutex);
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
vsx_main_thread_free(struct vsx_main_thread *mt)
{
        if (mt->have_timeout_thread) {
                pthread_mutex_lock(&mt->mutex);
                mt->timeout_thread_should_quit = true;
                pthread_cond_signal(&mt->cond);
                pthread_mutex_unlock(&mt->mutex);

                pthread_join(mt->timeout_thread, NULL /* retval */);

                mt->have_timeout_thread = false;
        }

        if (mt->have_cond) {
                pthread_cond_destroy(&mt->cond);
                mt->have_cond = false;
        }

        free_tokens(mt->queue);
        mt->queue = NULL;
        free_tokens(mt->freed_tokens);
        mt->freed_tokens = NULL;
        free_tokens(mt->timeout_queue);
        mt->timeout_queue = NULL;

        pthread_mutex_destroy(&mt->mutex);

        vsx_free(mt);
}
