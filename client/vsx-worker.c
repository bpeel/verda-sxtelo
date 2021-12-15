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

#include "vsx-worker.h"

#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <stdint.h>

#include "vsx-util.h"
#include "vsx-file-error.h"
#include "vsx-monotonic.h"
#include "vsx-thread.h"

struct vsx_worker {
        struct vsx_connection *connection;

        bool thread_created;
        pthread_t thread;
        pthread_mutex_t mutex;

        int wakeup_fds[2];
        bool wakeup_queued;

        int64_t wakeup_timestamp;

        struct pollfd poll_fds[2];

        struct vsx_listener event_listener;

        bool quit;
};

static void
wake_up_thread_locked(struct vsx_worker *worker)
{
        if (worker->wakeup_queued)
                return;

        static const uint8_t byte = 'W';

        write(worker->wakeup_fds[1], &byte, 1);

        worker->wakeup_queued = true;
}

static void
event_cb(struct vsx_listener *listener,
         void *user_data)
{
        struct vsx_worker *worker = vsx_container_of(listener,
                                                     struct vsx_worker,
                                                     event_listener);
        const struct vsx_connection_event *event = user_data;

        switch (event->type) {
        case VSX_CONNECTION_EVENT_TYPE_POLL_CHANGED:
                worker->wakeup_timestamp = event->poll_changed.wakeup_time;
                worker->poll_fds[1].fd = event->poll_changed.fd;
                worker->poll_fds[1].events = event->poll_changed.events;
                wake_up_thread_locked(worker);
                break;
        default:
                break;
        }
}

static void *
thread_func(void *user_data)
{
        struct vsx_worker *worker = user_data;

        vsx_worker_lock(worker);

        worker->wakeup_timestamp = INT64_MAX;

        worker->poll_fds[0].fd = worker->wakeup_fds[0];
        worker->poll_fds[0].events = POLLIN;

        worker->poll_fds[1].fd = -1;

        while (!worker->quit) {
                for (int i = 0; i < VSX_N_ELEMENTS(worker->poll_fds); i++)
                        worker->poll_fds[i].revents = 0;

                int timeout;

                if (worker->wakeup_timestamp >= INT64_MAX) {
                        timeout = -1;
                } else {
                        int64_t now = vsx_monotonic_get();

                        if (worker->wakeup_timestamp <= now) {
                                timeout = 0;
                        } else {
                                timeout = ((worker->wakeup_timestamp - now) /
                                           1000) + 1;
                        }
                }

                vsx_worker_unlock(worker);

                int poll_ret = poll(worker->poll_fds,
                                    worker->poll_fds[1].fd == -1 ? 1 : 2,
                                    timeout);

                vsx_worker_lock(worker);

                if (poll_ret == -1) {
                        if (errno == EINTR)
                                continue;
                        break;
                }

                if (worker->quit)
                        break;

                if (worker->poll_fds[0].revents) {
                        uint8_t byte;

                        int got = read(worker->wakeup_fds[0], &byte, 1);

                        if (got == -1) {
                                if (errno != EINTR)
                                        break;
                        } else if (got == 0) {
                                break;
                        } else {
                                worker->wakeup_queued = false;
                        }
                }

                vsx_connection_wake_up(worker->connection,
                                       worker->poll_fds[1].revents);
        }

        vsx_list_remove(&worker->event_listener.link);

        vsx_worker_unlock(worker);

        return NULL;
}

static void
init_event_listener(struct vsx_worker *worker)
{
        worker->event_listener.notify = event_cb;

        struct vsx_signal *signal =
                vsx_connection_get_event_signal(worker->connection);

        vsx_signal_add(signal, &worker->event_listener);
}

struct vsx_worker *
vsx_worker_new(struct vsx_connection *connection,
               struct vsx_error **error)
{
        struct vsx_worker *worker = vsx_calloc(sizeof *worker);

        worker->connection = connection;

        pthread_mutex_init(&worker->mutex, NULL /* attr */);

        if (pipe(worker->wakeup_fds) == -1) {
                worker->wakeup_fds[0] = -1;
                worker->wakeup_fds[1] = -1;

                vsx_file_error_set(error,
                                   errno,
                                   "Error creating wakeup pipe: %s",
                                   strerror(errno));
                goto error;
        }

        int thread_ret = vsx_thread_create(&worker->thread,
                                           NULL, /* attr */
                                           thread_func,
                                           worker);

        if (thread_ret != 0) {
                vsx_file_error_set(error,
                                   thread_ret,
                                   "Error creating thread: %s",
                                   strerror(thread_ret));
                goto error;
        }

        worker->thread_created = true;

        init_event_listener(worker);

        return worker;

error:
        vsx_worker_free(worker);
        return NULL;
}

void
vsx_worker_lock(struct vsx_worker *worker)
{
        pthread_mutex_lock(&worker->mutex);
}

void
vsx_worker_unlock(struct vsx_worker *worker)
{
        pthread_mutex_unlock(&worker->mutex);
}

void
vsx_worker_free(struct vsx_worker *worker)
{
        if (worker->thread_created) {
                vsx_worker_lock(worker);
                worker->quit = true;
                wake_up_thread_locked(worker);
                vsx_worker_unlock(worker);

                pthread_join(worker->thread, NULL /* retval */);
        }

        for (int i = 0; i < VSX_N_ELEMENTS(worker->wakeup_fds); i++) {
                if (worker->wakeup_fds[i] != -1)
                        vsx_close(worker->wakeup_fds[i]);
        }

        pthread_mutex_destroy(&worker->mutex);

        vsx_free(worker);
}
