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
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include "gml-log.h"
#include "gml-main-context.h"

static int gml_log_fd = -1;
static GString *gml_log_buffer = NULL;
static GmlMainContextSource *gml_log_source = NULL;

gboolean
gml_log_available (void)
{
  return gml_log_fd != -1;
}

static void
gml_log_update_poll (void)
{
  GmlMainContextPollFlags poll_flags;

  if (gml_log_buffer->len > 0)
    poll_flags = GML_MAIN_CONTEXT_POLL_OUT;
  else
    poll_flags = 0;

  gml_main_context_modify_poll (gml_log_source, poll_flags);
}

void
gml_log (const char *format,
         ...)
{
  va_list ap;
  GTimeVal now;
  char *now_string;

  if (gml_log_fd == -1)
    return;

  g_string_append_c (gml_log_buffer, '[');

  g_get_current_time (&now);
  now_string = g_time_val_to_iso8601 (&now);
  g_string_append (gml_log_buffer, now_string);
  g_free (now_string);

  g_string_append (gml_log_buffer, "] ");

  va_start (ap, format);
  g_string_append_vprintf (gml_log_buffer, format, ap);
  va_end (ap);

  g_string_append_c (gml_log_buffer, '\n');

  gml_log_update_poll ();
}

static void
gml_log_free_resources (void)
{
  if (gml_log_source)
    {
      gml_main_context_remove_source (gml_log_source);
      gml_log_source = NULL;
    }

  if (gml_log_fd != -1)
    {
      close (gml_log_fd);
      gml_log_fd = -1;
    }

  if (gml_log_buffer)
    {
      g_string_free (gml_log_buffer, TRUE);
      gml_log_buffer = NULL;
    }
}

static void
gml_log_poll_cb (GmlMainContextSource *source,
                 int fd,
                 GmlMainContextPollFlags flags,
                 void *user_data)
{
  ssize_t wrote;

  wrote = write (fd, gml_log_buffer->str, gml_log_buffer->len);

  if (wrote == -1)
    {
      if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
        {
          g_warning ("write failed on log file");
          gml_log_free_resources ();
        }
    }
  else
    {
      /* Move any remaining data to the beginning */
      memmove (gml_log_buffer->str,
               gml_log_buffer->str + wrote,
               gml_log_buffer->len - wrote);
      g_string_set_size (gml_log_buffer, gml_log_buffer->len - wrote);

      gml_log_update_poll ();
    }
}

static int
gml_log_set_non_blocking (int fd, gboolean value)
{
  int flags;

  flags = fcntl (fd, F_GETFL);

  if (flags == -1)
    return -1;

  if (value)
    flags |= O_NONBLOCK;
  else
    flags &= ~O_NONBLOCK;

  return fcntl (fd, F_SETFL, flags);
}

gboolean
gml_log_set_file (const char *filename,
                  GError **error)
{
  int fd;

  fd = open (filename, O_WRONLY | O_CREAT | O_APPEND,
             S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

  if (fd == -1)
    {
      g_set_error_literal (error,
                           G_FILE_ERROR,
                           g_file_error_from_errno (errno),
                           strerror (errno));
      return FALSE;
    }

  if (gml_log_set_non_blocking (fd, TRUE) == -1)
    {
      g_set_error_literal (error,
                           G_FILE_ERROR,
                           g_file_error_from_errno (errno),
                           strerror (errno));
      close (fd);
      return FALSE;
    }

  gml_log_free_resources ();

  gml_log_fd = fd;
  gml_log_buffer = g_string_new (NULL);
  gml_log_source = gml_main_context_add_poll (NULL /* default context */,
                                              fd,
                                              0,
                                              gml_log_poll_cb,
                                              NULL);

  return TRUE;
}

void
gml_log_close (void)
{
  if (gml_log_fd == -1)
    return;

  /* Try to flush all of the data in blocking mode before closing */
  if (gml_log_buffer->len > 0
      && gml_log_set_non_blocking (gml_log_fd, FALSE) != -1)
    {
      const char *pos = gml_log_buffer->str;
      const char *end = gml_log_buffer->str + gml_log_buffer->len;

      while (pos < end)
        {
          int wrote;

          wrote = write (gml_log_fd, pos, end - pos);

          if (wrote == -1)
            {
              if (errno != EINTR)
                break;
            }
          else
            pos += wrote;
        }
    }

  gml_log_free_resources ();
}
