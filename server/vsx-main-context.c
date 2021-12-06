/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2011, 2013  Neil Roberts
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>

#include "vsx-main-context.h"
#include "vsx-list.h"
#include "vsx-slice.h"

/* This is a simple replacement for the GMainLoop which uses
   epoll. The hope is that it will scale to more connections easily
   because it doesn't use poll which needs to upload the set of file
   descriptors every time it blocks and it doesn't have to walk the
   list of file descriptors to find out which object it belongs to */

typedef struct _VsxMainContextBucket VsxMainContextBucket;

struct _VsxMainContext
{
  int epoll_fd;
  /* Number of sources that are currently attached. This is used so we
     can size the array passed to epoll_wait to ensure it's possible
     to process an event for every single source */
  unsigned int n_sources;
  /* Array for receiving events */
  GArray *events;

  /* List of quit sources. All of these get invoked when a quit signal
     is received */
  VsxList quit_sources;

  VsxMainContextSource *quit_pipe_source;
  int quit_pipe[2];
  void (* old_int_handler) (int);
  void (* old_term_handler) (int);

  bool monotonic_time_valid;
  int64_t monotonic_time;

  VsxList buckets;
  int64_t last_timer_time;

  struct vsx_slice_allocator source_allocator;
};

struct _VsxMainContextSource
{
  enum
  {
    VSX_MAIN_CONTEXT_POLL_SOURCE,
    VSX_MAIN_CONTEXT_TIMER_SOURCE,
    VSX_MAIN_CONTEXT_QUIT_SOURCE
  } type;

  union
  {
    /* Poll sources */
    struct
    {
      int fd;
      VsxMainContextPollFlags current_flags;
    };

    /* Quit sources */
    struct
    {
      VsxList quit_link;
    };

    /* Timer sources */
    struct
    {
      VsxMainContextBucket *bucket;
      VsxList timer_link;
      bool busy;
      bool removed;
    };
  };

  gpointer user_data;
  void *callback;

  VsxMainContext *mc;
};

struct _VsxMainContextBucket
{
  VsxList link;
  VsxList sources;
  int minutes;
  int minutes_passed;
};

static VsxMainContext *vsx_main_context_default = NULL;

VsxMainContext *
vsx_main_context_get_default (GError **error)
{
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  if (vsx_main_context_default == NULL)
    vsx_main_context_default = vsx_main_context_new (error);

  return vsx_main_context_default;
}

static VsxMainContext *
vsx_main_context_get_default_or_abort (void)
{
  VsxMainContext *mc;
  GError *error = NULL;

  mc = vsx_main_context_get_default (&error);

  if (mc == NULL)
    {
      fprintf (stderr, "failed to create default main context: %s\n",
               error->message);
      g_clear_error (&error);
      exit (1);
    }

  return mc;
}

VsxMainContext *
vsx_main_context_new (GError **error)
{
  int fd;

  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  fd = epoll_create (16);

  if (fd == -1)
    {
      if (errno == EINVAL)
        g_set_error (error,
                     VSX_MAIN_CONTEXT_ERROR,
                     VSX_MAIN_CONTEXT_ERROR_UNSUPPORTED,
                     "epoll is unsupported on this system");
      else
        g_set_error (error,
                     VSX_MAIN_CONTEXT_ERROR,
                     VSX_MAIN_CONTEXT_ERROR_UNKNOWN,
                     "failed to create an epoll descriptor: %s",
                     strerror (errno));

      return NULL;
    }
  else
    {
      VsxMainContext *mc = g_new (VsxMainContext, 1);

      vsx_slice_allocator_init (&mc->source_allocator,
                                sizeof (VsxMainContextSource),
                                alignof (VsxMainContextSource));

      mc->epoll_fd = fd;
      mc->n_sources = 0;
      mc->events = g_array_new (false, false, sizeof (struct epoll_event));
      mc->monotonic_time_valid = false;
      vsx_list_init (&mc->quit_sources);
      mc->quit_pipe_source = NULL;
      vsx_list_init (&mc->buckets);
      mc->last_timer_time = vsx_main_context_get_monotonic_clock (mc);

      return mc;
    }
}

static uint32_t
get_epoll_events (VsxMainContextPollFlags flags)
{
  uint32_t events = 0;

  if (flags & VSX_MAIN_CONTEXT_POLL_IN)
    events |= EPOLLIN | EPOLLRDHUP;
  if (flags & VSX_MAIN_CONTEXT_POLL_OUT)
    events |= EPOLLOUT;

  return events;
}

VsxMainContextSource *
vsx_main_context_add_poll (VsxMainContext *mc,
                           int fd,
                           VsxMainContextPollFlags flags,
                           VsxMainContextPollCallback callback,
                           void *user_data)
{
  struct epoll_event event;

  if (mc == NULL)
    mc = vsx_main_context_get_default_or_abort ();

  VsxMainContextSource *source = vsx_slice_alloc (&mc->source_allocator);

  source->mc = mc;
  source->fd = fd;
  source->callback = callback;
  source->type = VSX_MAIN_CONTEXT_POLL_SOURCE;
  source->user_data = user_data;

  event.events = get_epoll_events (flags);
  event.data.ptr = source;

  if (epoll_ctl (mc->epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1)
    g_warning ("EPOLL_CTL_ADD failed: %s", strerror (errno));

  source->current_flags = flags;

  mc->n_sources++;

  return source;
}

void
vsx_main_context_modify_poll (VsxMainContextSource *source,
                              VsxMainContextPollFlags flags)
{
  struct epoll_event event;

  g_return_if_fail (source->type == VSX_MAIN_CONTEXT_POLL_SOURCE);

  if (source->current_flags == flags)
    return;

  event.events = get_epoll_events (flags);
  event.data.ptr = source;

  if (epoll_ctl (source->mc->epoll_fd, EPOLL_CTL_MOD, source->fd, &event) == -1)
    g_warning ("EPOLL_CTL_MOD failed: %s", strerror (errno));

  source->current_flags = flags;
}

static void
vsx_main_context_quit_pipe_cb (VsxMainContextSource *source,
                               int fd,
                               VsxMainContextPollFlags flags,
                               void *user_data)
{
  VsxMainContext *mc = user_data;
  uint8_t byte;

  if (read (mc->quit_pipe[0], &byte, sizeof (byte)) == -1)
    {
      if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        g_warning ("Read from quit pipe failed: %s", strerror (errno));
    }
  else
    {
      VsxMainContextSource *quit_source;

      vsx_list_for_each (quit_source, &mc->quit_sources, quit_link)
        {
          VsxMainContextQuitCallback callback = quit_source->callback;

          callback (quit_source, quit_source->user_data);
        }
    }
}

static void
vsx_main_context_quit_signal_cb (int signum)
{
  VsxMainContext *mc = vsx_main_context_get_default_or_abort ();
  uint8_t byte = 42;

  while (write (mc->quit_pipe[1], &byte, 1) == -1
         && errno == EINTR);
}

VsxMainContextSource *
vsx_main_context_add_quit (VsxMainContext *mc,
                           VsxMainContextQuitCallback callback,
                           void *user_data)
{
  if (mc == NULL)
    mc = vsx_main_context_get_default_or_abort ();

  VsxMainContextSource *source = vsx_slice_alloc (&mc->source_allocator);

  source->mc = mc;
  source->callback = callback;
  source->type = VSX_MAIN_CONTEXT_QUIT_SOURCE;
  source->user_data = user_data;

  vsx_list_insert (&mc->quit_sources, &source->quit_link);

  mc->n_sources++;

  if (mc->quit_pipe_source == NULL)
    {
      if (pipe (mc->quit_pipe) == -1)
        g_warning ("Failed to create quit pipe: %s", strerror (errno));
      else
        {
          mc->quit_pipe_source
            = vsx_main_context_add_poll (mc, mc->quit_pipe[0],
                                         VSX_MAIN_CONTEXT_POLL_IN,
                                         vsx_main_context_quit_pipe_cb,
                                         mc);

          mc->old_int_handler =
            signal (SIGINT, vsx_main_context_quit_signal_cb);
          mc->old_term_handler =
            signal (SIGTERM, vsx_main_context_quit_signal_cb);
        }
    }

  return source;
}

static VsxMainContextBucket *
get_bucket (VsxMainContext *mc,
            int minutes)
{
  VsxMainContextBucket *bucket;

  vsx_list_for_each (bucket, &mc->buckets, link)
    {
      if (bucket->minutes == minutes)
        return bucket;
    }

  bucket = vsx_alloc (sizeof *bucket);
  vsx_list_init (&bucket->sources);
  bucket->minutes = minutes;
  bucket->minutes_passed = 0;
  vsx_list_insert (&mc->buckets, &bucket->link);

  return bucket;
}

VsxMainContextSource *
vsx_main_context_add_timer (VsxMainContext *mc,
                            int minutes,
                            VsxMainContextTimerCallback callback,
                            void *user_data)
{
  if (mc == NULL)
    mc = vsx_main_context_get_default_or_abort ();

  VsxMainContextSource *source = vsx_slice_alloc (&mc->source_allocator);

  source->mc = mc;
  source->bucket = get_bucket (mc, minutes);
  source->callback = callback;
  source->type = VSX_MAIN_CONTEXT_TIMER_SOURCE;
  source->user_data = user_data;
  source->removed = false;
  source->busy = false;

  vsx_list_insert (&source->bucket->sources, &source->timer_link);

  mc->n_sources++;

  return source;
}

void
vsx_main_context_remove_source (VsxMainContextSource *source)
{
  VsxMainContext *mc = source->mc;
  struct epoll_event event;

  switch (source->type)
    {
    case VSX_MAIN_CONTEXT_POLL_SOURCE:
      if (epoll_ctl (mc->epoll_fd, EPOLL_CTL_DEL, source->fd, &event) == -1)
        g_warning ("EPOLL_CTL_DEL failed: %s", strerror (errno));
      vsx_slice_free (&mc->source_allocator, source);
      break;

    case VSX_MAIN_CONTEXT_QUIT_SOURCE:
      vsx_list_remove (&source->quit_link);
      vsx_slice_free (&mc->source_allocator, source);
      break;

    case VSX_MAIN_CONTEXT_TIMER_SOURCE:
      /* Timer sources need to be able to be removed while iterating
       * the source list to emit, so we need to handle them specially
       * during iteration. */

      g_assert(!source->removed);

      if (source->busy)
        {
          source->removed = true;
        }
      else
        {
          vsx_list_remove (&source->timer_link);
          vsx_slice_free (&mc->source_allocator, source);
        }

      break;
    }

  mc->n_sources--;
}

static int
get_timeout (VsxMainContext *mc)
{
  VsxMainContextBucket *bucket;
  int min_minutes;
  int64_t elapsed, elapsed_minutes;

  min_minutes = INT_MAX;

  vsx_list_for_each (bucket, &mc->buckets, link)
    {
      if (vsx_list_empty (&bucket->sources))
        continue;

      int minutes_to_wait = bucket->minutes - bucket->minutes_passed;

      if (minutes_to_wait < min_minutes)
        min_minutes = minutes_to_wait;
    }

  if (min_minutes == INT_MAX)
    return -1;

  elapsed = vsx_main_context_get_monotonic_clock (mc) - mc->last_timer_time;
  elapsed_minutes = elapsed / 60000000;

  /* If we've already waited enough time then don't wait any further time */
  if (elapsed_minutes >= min_minutes)
    return 0;

  /* Subtract the number of minutes we've already waited */
  min_minutes -= (int) elapsed_minutes;

  return (60000 - (elapsed / 1000 % 60000) + (min_minutes - 1) * 60000);
}

static void
check_timer_sources (VsxMainContext *mc)
{
  VsxMainContextBucket *bucket;
  int64_t now;
  int64_t elapsed_minutes;

  if (vsx_list_empty (&mc->buckets))
    return;

  now = vsx_main_context_get_monotonic_clock (mc);
  elapsed_minutes = (now - mc->last_timer_time) / 60000000;
  mc->last_timer_time += elapsed_minutes * 60000000;

  if (elapsed_minutes < 1)
    return;

  /* Collect all of the sources to emit into a list and mark them as
   * busy. That way if they are removed they will just be marked as
   * removed instead of actually modifying the bucket’s list. That way
   * any timers can be removed as a result of invoking any callback.
   */
  VsxList to_emit;
  vsx_list_init (&to_emit);

  vsx_list_for_each (bucket, &mc->buckets, link)
    {
      if (bucket->minutes_passed + elapsed_minutes >= bucket->minutes)
        {
          vsx_list_insert_list (&to_emit, &bucket->sources);
          bucket->minutes_passed = 0;
          vsx_list_init (&bucket->sources);
        }
      else
        {
          bucket->minutes_passed += elapsed_minutes;
        }
    }

  VsxMainContextSource *source, *tmp_source;

  vsx_list_for_each (source, &to_emit, timer_link)
    {
      source->busy = true;
    }

  vsx_list_for_each (source, &to_emit, timer_link)
    {
      if (source->removed)
        continue;
      VsxMainContextTimerCallback callback = source->callback;
      callback (source, source->user_data);
    }

  vsx_list_for_each_safe (source, tmp_source, &to_emit, timer_link)
    {
      if (source->removed)
        {
          vsx_slice_free (&mc->source_allocator, source);
        }
      else
        {
          /* Remove from tmp_source before inserting into the bucket.
           * Even though we are just going to discard the to_emit
           * list, this is still important in order to have the
           * correct links in the neighbouring nodes.
           */
          vsx_list_remove (&source->timer_link);
          vsx_list_insert (&source->bucket->sources,
                           &source->timer_link);
          source->busy = false;
        }
    }
}

void
vsx_main_context_poll (VsxMainContext *mc)
{
  int n_events;

  if (mc == NULL)
    mc = vsx_main_context_get_default_or_abort ();

  g_array_set_size (mc->events, mc->n_sources);

  n_events = epoll_wait (mc->epoll_fd,
                         &g_array_index (mc->events,
                                         struct epoll_event,
                                         0),
                         mc->n_sources,
                         get_timeout (mc));

  /* Once we've polled we can assume that some time has passed so our
     cached value of the monotonic clock is no longer valid */
  mc->monotonic_time_valid = false;

  if (n_events == -1)
    {
      if (errno != EINTR)
        g_warning ("epoll_wait failed: %s", strerror (errno));
    }
  else
    {
      int i;

      for (i = 0; i < n_events; i++)
        {
          struct epoll_event *event = &g_array_index (mc->events,
                                                      struct epoll_event,
                                                      i);
          VsxMainContextSource *source = event->data.ptr;

          switch (source->type)
            {
            case VSX_MAIN_CONTEXT_POLL_SOURCE:
              {
                VsxMainContextPollCallback callback = source->callback;
                VsxMainContextPollFlags flags = 0;

                if (event->events & EPOLLOUT)
                  flags |= VSX_MAIN_CONTEXT_POLL_OUT;
                if (event->events & (EPOLLIN | EPOLLRDHUP))
                  flags |= VSX_MAIN_CONTEXT_POLL_IN;
                if (event->events & EPOLLHUP)
                  {
                    /* If the source is polling for read then we'll
                     * just mark it as ready for reading so that any
                     * error or EOF will be handled by the read call
                     * instead of immediately aborting */
                    if (source->current_flags & VSX_MAIN_CONTEXT_POLL_IN)
                      flags |= VSX_MAIN_CONTEXT_POLL_IN;
                    else
                      flags |= VSX_MAIN_CONTEXT_POLL_ERROR;
                  }
                if (event->events & EPOLLERR)
                  flags |= VSX_MAIN_CONTEXT_POLL_ERROR;

                callback (source, source->fd, flags, source->user_data);
              }
              break;

            case VSX_MAIN_CONTEXT_QUIT_SOURCE:
            case VSX_MAIN_CONTEXT_TIMER_SOURCE:
              g_warn_if_reached ();
              break;
            }
        }

      check_timer_sources (mc);
    }
}

int64_t
vsx_main_context_get_monotonic_clock (VsxMainContext *mc)
{
  if (mc == NULL)
    mc = vsx_main_context_get_default_or_abort ();

  /* Because in theory the program doesn't block between calls to
     poll, we can act as if no time passes between calls to
     epoll. That way we can cache the clock value instead of having to
     do a system call every time we need it */
  if (!mc->monotonic_time_valid)
    {
      mc->monotonic_time = g_get_monotonic_time ();
      mc->monotonic_time_valid = true;
    }

  return mc->monotonic_time;
}

static void
free_buckets (VsxMainContext *mc)
{
  VsxMainContextBucket *bucket, *tmp;

  vsx_list_for_each_safe (bucket, tmp, &mc->buckets, link)
    {
      g_assert (vsx_list_empty (&bucket->sources));
      vsx_free (bucket);
    }
}

void
vsx_main_context_free (VsxMainContext *mc)
{
  g_return_if_fail (mc != NULL);

  if (mc->quit_pipe_source)
    {
      signal (SIGINT, mc->old_int_handler);
      signal (SIGTERM, mc->old_term_handler);
      vsx_main_context_remove_source (mc->quit_pipe_source);
      close (mc->quit_pipe[0]);
      close (mc->quit_pipe[1]);
    }

  if (mc->n_sources > 0)
    g_warning ("Sources still remain on a main context that is being freed");

  free_buckets (mc);

  g_array_free (mc->events, true);
  close (mc->epoll_fd);

  vsx_slice_allocator_destroy (&mc->source_allocator);

  g_free (mc);

  if (mc == vsx_main_context_default)
    vsx_main_context_default = NULL;
}

GQuark
vsx_main_context_error_quark (void)
{
  return g_quark_from_static_string ("vsx-main-context-error");
}
