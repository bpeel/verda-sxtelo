/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2011  Neil Roberts
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

#include "vsx-main-context.h"

/* This is a simple replacement for the GMainLoop which uses
   epoll. The hope is that it will scale to more connections easily
   because it doesn't use poll which needs to upload the set of file
   descriptors every time it blocks and it doesn't have to walk the
   list of file descriptors to find out which object it belongs to */

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
  GSList *quit_sources;

  VsxMainContextSource *quit_pipe_source;
  int quit_pipe[2];
  void (* old_int_handler) (int);
  void (* old_term_handler) (int);

  gboolean monotonic_time_valid;
  gint64 monotonic_time;
};

struct _VsxMainContextSource
{
  enum
  {
    VSX_MAIN_CONTEXT_POLL_SOURCE,
    VSX_MAIN_CONTEXT_QUIT_SOURCE
  } type;

  int fd;

  gpointer user_data;
  void *callback;

  VsxMainContextPollFlags current_flags;

  VsxMainContext *mc;
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

      mc->epoll_fd = fd;
      mc->n_sources = 0;
      mc->events = g_array_new (FALSE, FALSE, sizeof (struct epoll_event));
      mc->monotonic_time_valid = FALSE;
      mc->quit_sources = NULL;
      mc->quit_pipe_source = NULL;

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
  VsxMainContextSource *source = g_slice_new (VsxMainContextSource);
  struct epoll_event event;

  if (mc == NULL)
    mc = vsx_main_context_get_default_or_abort ();

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
vsx_main_context_quit_foreach_cb (void *data,
                                  void *user_data)
{
  VsxMainContextSource *source = data;
  VsxMainContextQuitCallback callback = source->callback;

  callback (source, source->user_data);
}

static void
vsx_main_context_quit_pipe_cb (VsxMainContextSource *source,
                               int fd,
                               VsxMainContextPollFlags flags,
                               void *user_data)
{
  VsxMainContext *mc = user_data;
  guint8 byte;

  if (read (mc->quit_pipe[0], &byte, sizeof (byte)) == -1)
    {
      if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        g_warning ("Read from quit pipe failed: %s", strerror (errno));
    }
  else
    g_slist_foreach (mc->quit_sources, vsx_main_context_quit_foreach_cb, mc);
}

static void
vsx_main_context_quit_signal_cb (int signum)
{
  VsxMainContext *mc = vsx_main_context_get_default_or_abort ();
  guint8 byte = 42;

  while (write (mc->quit_pipe[1], &byte, 1) == -1
         && errno == EINTR);
}

VsxMainContextSource *
vsx_main_context_add_quit (VsxMainContext *mc,
                           VsxMainContextQuitCallback callback,
                           void *user_data)
{
  VsxMainContextSource *source = g_slice_new (VsxMainContextSource);

  if (mc == NULL)
    mc = vsx_main_context_get_default_or_abort ();

  source->mc = mc;
  source->fd = -1;
  source->callback = callback;
  source->type = VSX_MAIN_CONTEXT_QUIT_SOURCE;
  source->user_data = user_data;

  mc->quit_sources = g_slist_prepend (mc->quit_sources, source);

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
      break;

    case VSX_MAIN_CONTEXT_QUIT_SOURCE:
      mc->quit_sources = g_slist_remove (mc->quit_sources, source);
      break;
    }

  g_slice_free (VsxMainContextSource, source);

  mc->n_sources--;
}

void
vsx_main_context_poll (VsxMainContext *mc,
                       int timeout)
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
                         timeout);

  /* Once we've polled we can assume that some time has passed so our
     cached value of the monotonic clock is no longer valid */
  mc->monotonic_time_valid = FALSE;

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
                if (event->events & (EPOLLHUP | EPOLLERR))
                  flags |= VSX_MAIN_CONTEXT_POLL_ERROR;

                callback (source, source->fd, flags, source->user_data);
              }
              break;

            case VSX_MAIN_CONTEXT_QUIT_SOURCE:
              g_warn_if_reached ();
              break;
            }
        }
    }
}

gint64
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
      mc->monotonic_time_valid = TRUE;
    }

  return mc->monotonic_time;
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

  g_array_free (mc->events, TRUE);
  close (mc->epoll_fd);
  g_free (mc);

  if (mc == vsx_main_context_default)
    vsx_main_context_default = NULL;
}

GQuark
vsx_main_context_error_quark (void)
{
  return g_quark_from_static_string ("vsx-main-context-error");
}
