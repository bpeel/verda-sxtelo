/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2022  Neil Roberts
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

#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "vsx-util.h"
#include "vsx-monotonic.h"

struct harness {
        bool idle_queued;
};

static void
wakeup_cb(void *user_data)
{
        struct harness *harness = user_data;

        harness->idle_queued = true;
}

static void
free_harness(struct harness *harness)
{
        vsx_main_thread_set_wakeup_func(NULL, NULL);
        vsx_main_thread_clean_up();
        vsx_free(harness);
}

static struct harness *
create_harness(void)
{
        struct harness *harness = vsx_calloc(sizeof *harness);

        vsx_main_thread_set_wakeup_func(wakeup_cb, harness);

        return harness;
}

#define N_IDLES_PER_THREAD 1024

struct threaded_queue_event_closure {
        pthread_t self;
        int n_idles_invoked;
        bool succeeded;
};

static void
threaded_queue_idle_cb(void *user_data)
{
        struct threaded_queue_event_closure *closure = user_data;

        if (pthread_self() != closure->self) {
                fprintf(stderr, "idle callback invoked from wrong thread\n");
                closure->succeeded = false;
                return;
        }

        closure->n_idles_invoked++;
}

static void *
threaded_queue_thread_func(void *user_data)
{
        struct threaded_queue_event_closure *closure = user_data;

        for (int i = 0; i < N_IDLES_PER_THREAD; i++)
                vsx_main_thread_queue_idle(threaded_queue_idle_cb, closure);

        return NULL;
}

static bool
test_threaded_queue_event(void)
{
        struct harness *harness = create_harness();

        struct threaded_queue_event_closure closure = {
                .n_idles_invoked = 0,
                .succeeded = true,
                .self = pthread_self(),
        };

        bool ret = true;

        pthread_t threads[16];
        int n_threads;

        for (n_threads = 0; n_threads < VSX_N_ELEMENTS(threads); n_threads++) {
                int thread_ret = pthread_create(threads + n_threads,
                                                NULL, /* attr */
                                                threaded_queue_thread_func,
                                                &closure);

                if (thread_ret) {
                        fprintf(stderr,
                                "pthread_create failed: %s\n",
                                strerror(thread_ret));
                        ret = false;
                        break;
                }
        }

        for (int i = 0; i < n_threads; i++) {
                int join_ret = pthread_join(threads[i], NULL /* retval */);

                if (join_ret) {
                        fprintf(stderr,
                                "pthread_join failed: %s\n",
                                strerror(join_ret));
                        ret = false;
                }
        }

        if (closure.n_idles_invoked > 0) {
                fprintf(stderr,
                        "idle funcs were invoked before flushing the queue\n");
                ret = false;
        }

        if (!harness->idle_queued) {
                fprintf(stderr,
                        "no queue flush was requested after queuing events\n");
                ret = false;
        }

        vsx_main_thread_flush_idle_events();

        int expected_n_idles = VSX_N_ELEMENTS(threads) * N_IDLES_PER_THREAD;

        if (closure.n_idles_invoked != expected_n_idles) {
                fprintf(stderr,
                        "wrong number of idles invoked.\n"
                        " Expected: %i\n"
                        " Received: %i\n",
                        expected_n_idles,
                        closure.n_idles_invoked);
                ret = false;
        }

        if (!closure.succeeded)
                ret = false;

        free_harness(harness);

        return ret;
}

static bool
test_flush_empty(void)
{
        vsx_main_thread_flush_idle_events();
        vsx_main_thread_clean_up();

        return true;
}

static void
count_invocations_cb(void *user_data)
{
        int *invocation_count = user_data;

        (*invocation_count)++;
}

static bool
test_no_wakeup_func(void)
{
        int invocation_count = 0;

        vsx_main_thread_queue_idle(count_invocations_cb, &invocation_count);
        vsx_main_thread_flush_idle_events();

        bool ret = true;

        if (invocation_count > 1) {
                fprintf(stderr,
                        "idle callback invoked multiple times.\n");
                ret = false;
        } else if (invocation_count != 1) {
                fprintf(stderr,
                        "callback not invoked in test with no wakeup func.\n");
                ret = false;
        }

        vsx_main_thread_clean_up();

        return ret;
}

static bool
test_no_wakeup_func_timeout(void)
{
        int invocation_count = 0;

        vsx_main_thread_queue_timeout(0, /* microseconds */
                                      count_invocations_cb,
                                      &invocation_count);

        /* Sleep 100ms */
        struct timespec sleep_time = {
                .tv_sec = 0,
                .tv_nsec = 100 * 1000 * 1000L,
        };
        nanosleep(&sleep_time, NULL /* rem */);

        vsx_main_thread_flush_idle_events();

        bool ret = true;

        if (invocation_count > 1) {
                fprintf(stderr,
                        "idle callback invoked multiple times.\n");
                ret = false;
        } else if (invocation_count != 1) {
                fprintf(stderr,
                        "callback not invoked in test with no wakeup func.\n");
                ret = false;
        }

        vsx_main_thread_clean_up();

        return ret;
}

static bool
test_simple_queue_and_flush(struct harness *harness)
{
        int invocation_count = 0;

        vsx_main_thread_queue_idle(count_invocations_cb, &invocation_count);

        bool ret = true;

        if (!harness->idle_queued) {
                fprintf(stderr,
                        "No idle queue flush requested after queuing an "
                        "event.\n");
                ret = false;
        }

        if (invocation_count != 0) {
                fprintf(stderr,
                        "Idle callback invoked before flushing queue.\n");
                ret = false;
        }

        vsx_main_thread_flush_idle_events();

        if (invocation_count > 1) {
                fprintf(stderr,
                        "idle callback invoked multiple times.\n");
                ret = false;
        } else if (invocation_count != 1) {
                fprintf(stderr,
                        "callback not invoked in test with no wakeup func.\n");
                ret = false;
        }

        return ret;
}

static bool
test_use_freed_token(void)
{
        struct harness *harness = create_harness();

        bool ret = true;

        for (int i = 0; i < 2; i++) {
                if (!test_simple_queue_and_flush(harness)) {
                        ret = false;
                        break;
                }
        }

        free_harness(harness);

        return ret;
}

static bool
test_cancel(void)
{
        struct harness *harness = create_harness();

        int cancelled_invocation_count = 0;

        struct vsx_main_thread_token *token =
                vsx_main_thread_queue_idle(count_invocations_cb,
                                           &cancelled_invocation_count);

        vsx_main_thread_cancel_idle(token);

        bool ret = true;

        if (!test_simple_queue_and_flush(harness))
                ret = false;

        if (cancelled_invocation_count != 0) {
                fprintf(stderr,
                        "Cancelled idle event was invoked.\n");
                ret = false;
        }

        free_harness(harness);

        return ret;
}

static bool
test_dangling_tokens(void)
{
        struct vsx_main_thread_token *tokens[16];

        for (int i = 0; i < VSX_N_ELEMENTS(tokens); i++) {
                tokens[i] = vsx_main_thread_queue_idle(count_invocations_cb,
                                                       NULL);
        }

        /* Cancel half of them to ensure that both lists are freed */

        for (int i = 0; i < VSX_N_ELEMENTS(tokens) / 2; i++)
                vsx_main_thread_cancel_idle(tokens[i]);

        /* Check the timeout queue as well */

        vsx_main_thread_queue_timeout(10 * 1000 * 1000,
                                      count_invocations_cb,
                                      NULL /* user_data */);

        /* Clean up without flushing the queue */

        int64_t cleanup_start = vsx_monotonic_get();

        vsx_main_thread_clean_up();

        int64_t cleanup_time = vsx_monotonic_get() - cleanup_start;

        if (cleanup_time > 500 * 1000) {
                fprintf(stderr,
                        "Clean up with a dangling timeout took %f seconds\n",
                        cleanup_time / 1000.0f / 1000.0f);
                return false;
        }

        return true;
}

static bool
test_timeout(void)
{
        struct harness *harness = create_harness();

        bool ret = true;

        int invocation_counts[4];

        memset(invocation_counts, 0, sizeof invocation_counts);

        /* Create 4 timeout at 4 seconds, 2 seconds, 8 seconds and
         * 6 seconds. The funky order is to check that the timeout
         * list gets maintained in sorted order.
         */

        for (int i = 0; i < VSX_N_ELEMENTS(invocation_counts); i++) {
                int timeout_num = i ^ 1;
                int timeout_microseconds = (timeout_num + 1) * 2 * 1000 * 1000;

                vsx_main_thread_queue_timeout(timeout_microseconds,
                                              count_invocations_cb,
                                              invocation_counts + timeout_num);
        }

        for (int i = 0; i < VSX_N_ELEMENTS(invocation_counts); i++) {
                struct timespec sleep_time = {
                        .tv_sec = 1,
                        .tv_nsec = 500 * 1000 * 1000l,
                };

                harness->idle_queued = false;

                /* sleep for 1.5 seconds. This shouldn’t be enough to
                 * trigger the timeout.
                 */

                nanosleep(&sleep_time, NULL /* rem */);

                if (harness->idle_queued) {
                        fprintf(stderr,
                                "Idle queued before timeout should be "
                                "ready.\n");
                        ret = false;
                }

                /* sleep for 600 ms to push it over the limit */
                sleep_time.tv_sec = 0;
                sleep_time.tv_nsec += 100 * 1000 * 1000l;

                nanosleep(&sleep_time, NULL /* rem */);

                if (!harness->idle_queued) {
                        fprintf(stderr,
                                "Idle not queued even though enough time has "
                                "elapsed\n");
                        ret = false;
                }

                vsx_main_thread_flush_idle_events();

                if (invocation_counts[i] == 0) {
                        fprintf(stderr,
                                "Timeout not invoked after waiting the "
                                "time.\n");
                        ret = false;
                }

                for (int j = i + 1;
                     j < VSX_N_ELEMENTS(invocation_counts);
                     j++) {
                        if (invocation_counts[j] != 0) {
                                fprintf(stderr,
                                        "Timeout %i invoked early.\n",
                                        j);
                                ret = false;
                        }
                }
        }

        for (int i = 0; i < VSX_N_ELEMENTS(invocation_counts); i++) {
                if (invocation_counts[i] != 1) {
                        fprintf(stderr,
                                "Timeout %i invoked %i times.\n",
                                i,
                                invocation_counts[i]);
                        ret = false;
                }
        }

        free_harness(harness);

        return ret;
}

int
main(int argc, char **argv)
{
        int ret = EXIT_SUCCESS;

        if (!test_flush_empty())
                ret = EXIT_FAILURE;

        if (!test_no_wakeup_func())
                ret = EXIT_FAILURE;

        if (!test_no_wakeup_func_timeout())
                ret = EXIT_FAILURE;

        if (!test_threaded_queue_event())
                ret = EXIT_FAILURE;

        if (!test_use_freed_token())
                ret = EXIT_FAILURE;

        if (!test_cancel())
                ret = EXIT_FAILURE;

        if (!test_timeout())
                ret = EXIT_FAILURE;

        if (!test_dangling_tokens())
                ret = EXIT_FAILURE;

        return ret;
}
