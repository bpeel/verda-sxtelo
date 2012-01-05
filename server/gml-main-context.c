/*
 * Gemelo - A server for chatting with strangers in a foreign language
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

#include "gml-main-context.h"

/* This is a simple replacement for the GMainLoop which uses
   epoll. The hope is that it will scale to more connections easily
   because it doesn't use poll which needs to upload the set of file
   descriptors every time it blocks and it doesn't have to walk the
   list of file descriptors to find out which object it belongs to */

struct _GmlMainContext
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

  GmlMainContextSource *quit_pipe_source;
  int quit_pipe[2];
  void (* old_int_handler) (int);
  void (* old_term_handler) (int);

  gboolean monotonic_time_valid;
  gint64 monotonic_time;
};

struct _GmlMainContextSource
{
  enum
  {
    GML_MAIN_CONTEXT_POLL_SOURCE,
    GML_MAIN_CONTEXT_QUIT_SOURCE
  } type;

  int fd;

  gpointer user_data;
  void *callback;

  GmlMainContextPollFlags current_flags;

  GmlMainContext *mc;
};

static GmlMainContext *gml_main_context_default = NULL;

GmlMainContext *
gml_main_context_get_default (GError **error)
{
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  if (gml_main_context_default == NULL)
    gml_main_context_default = gml_main_context_new (error);

  return gml_main_context_default;
}

static GmlMainContext *
gml_main_context_get_default_or_abort (void)
{
  GmlMainContext *mc;
  GError *error = NULL;

  mc = gml_main_context_get_default (&error);

  if (mc == NULL)
    {
      fprintf (stderr, "failed to create default main context: %s\n",
               error->message);
      g_clear_error (&error);
      exit (1);
    }

  return mc;
}

GmlMainContext *
gml_main_context_new (GError **error)
{
  int fd;

  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  fd = epoll_create (16);

  if (fd == -1)
    {
      if (errno == EINVAL)
        g_set_error (error,
                     GML_MAIN_CONTEXT_ERROR,
                     GML_MAIN_CONTEXT_ERROR_UNSUPPORTED,
                     "epoll is unsupported on this system");
      else
        g_set_error (error,
                     GML_MAIN_CONTEXT_ERROR,
                     GML_MAIN_CONTEXT_ERROR_UNKNOWN,
                     "failed to create an epoll descriptor: %s",
                     strerror (errno));

      return NULL;
    }
  else
    {
      GmlMainContext *mc = g_new (GmlMainContext, 1);

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
get_epoll_events (GmlMainContextPollFlags flags)
{
  uint32_t events = 0;

  if (flags & GML_MAIN_CONTEXT_POLL_IN)
    events |= EPOLLIN | EPOLLRDHUP;
  if (flags & GML_MAIN_CONTEXT_POLL_OUT)
    events |= EPOLLOUT;

  return events;
}

GmlMainContextSource *
gml_main_context_add_poll (GmlMainContext *mc,
                           int fd,
                           GmlMainContextPollFlags flags,
                           GmlMainContextPollCallback callback,
                           void *user_data)
{
  GmlMainContextSource *source = g_slice_new (GmlMainContextSource);
  struct epoll_event event;

  if (mc == NULL)
    mc = gml_main_context_get_default_or_abort ();

  source->mc = mc;
  source->fd = fd;
  source->callback = callback;
  source->type = GML_MAIN_CONTEXT_POLL_SOURCE;
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
gml_main_context_modify_poll (GmlMainContextSource *source,
                              GmlMainContextPollFlags flags)
{
  struct epoll_event event;

  g_return_if_fail (source->type == GML_MAIN_CONTEXT_POLL_SOURCE);

  if (source->current_flags == flags)
    return;

  event.events = get_epoll_events (flags);
  event.data.ptr = source;

  if (epoll_ctl (source->mc->epoll_fd, EPOLL_CTL_MOD, source->fd, &event) == -1)
    g_warning ("EPOLL_CTL_MOD failed: %s", strerror (errno));

  source->current_flags = flags;
}

static void
gml_main_context_quit_foreach_cb (void *data,
                                  void *user_data)
{
  GmlMainContextSource *source = data;
  GmlMainContextQuitCallback callback = source->callback;

  callback (source, source->user_data);
}

static void
gml_main_context_quit_pipe_cb (GmlMainContextSource *source,
                               int fd,
                               GmlMainContextPollFlags flags,
                               void *user_data)
{
  GmlMainContext *mc = user_data;
  guint8 byte;

  if (read (mc->quit_pipe[0], &byte, sizeof (byte)) == -1)
    {
      if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        g_warning ("Read from quit pipe failed: %s", strerror (errno));
    }
  else
    g_slist_foreach (mc->quit_sources, gml_main_context_quit_foreach_cb, mc);
}

static void
gml_main_context_quit_signal_cb (int signum)
{
  GmlMainContext *mc = gml_main_context_get_default_or_abort ();
  guint8 byte = 42;

  while (write (mc->quit_pipe[1], &byte, 1) == -1
         && errno == EINTR);
}

GmlMainContextSource *
gml_main_context_add_quit (GmlMainContext *mc,
                           GmlMainContextQuitCallback callback,
                           void *user_data)
{
  GmlMainContextSource *source = g_slice_new (GmlMainContextSource);

  if (mc == NULL)
    mc = gml_main_context_get_default_or_abort ();

  source->mc = mc;
  source->fd = -1;
  source->callback = callback;
  source->type = GML_MAIN_CONTEXT_QUIT_SOURCE;
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
            = gml_main_context_add_poll (mc, mc->quit_pipe[0],
                                         GML_MAIN_CONTEXT_POLL_IN,
                                         gml_main_context_quit_pipe_cb,
                                         mc);

          mc->old_int_handler =
            signal (SIGINT, gml_main_context_quit_signal_cb);
          mc->old_term_handler =
            signal (SIGTERM, gml_main_context_quit_signal_cb);
        }
    }

  return source;
}

void
gml_main_context_remove_source (GmlMainContextSource *source)
{
  GmlMainContext *mc = source->mc;
  struct epoll_event event;

  switch (source->type)
    {
    case GML_MAIN_CONTEXT_POLL_SOURCE:
      if (epoll_ctl (mc->epoll_fd, EPOLL_CTL_DEL, source->fd, &event) == -1)
        g_warning ("EPOLL_CTL_DEL failed: %s", strerror (errno));
      break;

    case GML_MAIN_CONTEXT_QUIT_SOURCE:
      mc->quit_sources = g_slist_remove (mc->quit_sources, source);
      break;
    }

  g_slice_free (GmlMainContextSource, source);

  mc->n_sources--;
}

void
gml_main_context_poll (GmlMainContext *mc,
                       int timeout)
{
  int n_events;

  if (mc == NULL)
    mc = gml_main_context_get_default_or_abort ();

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
          GmlMainContextSource *source = event->data.ptr;

          switch (source->type)
            {
            case GML_MAIN_CONTEXT_POLL_SOURCE:
              {
                GmlMainContextPollCallback callback = source->callback;
                GmlMainContextPollFlags flags = 0;

                if (event->events & EPOLLOUT)
                  flags |= GML_MAIN_CONTEXT_POLL_OUT;
                if (event->events & (EPOLLIN | EPOLLRDHUP))
                  flags |= GML_MAIN_CONTEXT_POLL_IN;
                if (event->events & (EPOLLHUP | EPOLLERR))
                  flags |= GML_MAIN_CONTEXT_POLL_ERROR;

                callback (source, source->fd, flags, source->user_data);
              }
              break;

            case GML_MAIN_CONTEXT_QUIT_SOURCE:
              g_warn_if_reached ();
              break;
            }
        }
    }
}

gint64
gml_main_context_get_monotonic_clock (GmlMainContext *mc)
{
  if (mc == NULL)
    mc = gml_main_context_get_default_or_abort ();

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
gml_main_context_free (GmlMainContext *mc)
{
  g_return_if_fail (mc != NULL);

  if (mc->quit_pipe_source)
    {
      signal (SIGINT, mc->old_int_handler);
      signal (SIGTERM, mc->old_term_handler);
      gml_main_context_remove_source (mc->quit_pipe_source);
      close (mc->quit_pipe[0]);
      close (mc->quit_pipe[1]);
    }

  if (mc->n_sources > 0)
    g_warning ("Sources still remain on a main context that is being freed");

  g_array_free (mc->events, TRUE);
  close (mc->epoll_fd);
  g_free (mc);

  if (mc == gml_main_context_default)
    gml_main_context_default = NULL;
}

GQuark
gml_main_context_error_quark (void)
{
  return g_quark_from_static_string ("gml-main-context-error");
}