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
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "vsx-log.h"

static FILE *vsx_log_file = NULL;
static GString *vsx_log_buffer = NULL;
static GThread *vsx_log_thread = NULL;
static GMutex vsx_log_mutex;
static GCond vsx_log_cond;
static gboolean vsx_log_finished = FALSE;

gboolean
vsx_log_available (void)
{
  return vsx_log_file != NULL;
}

void
vsx_log (const char *format,
         ...)
{
  va_list ap;
  GTimeVal now;
  char *now_string;

  if (!vsx_log_available ())
    return;

  g_mutex_lock (&vsx_log_mutex);

  g_string_append_c (vsx_log_buffer, '[');

  g_get_current_time (&now);
  now_string = g_time_val_to_iso8601 (&now);
  g_string_append (vsx_log_buffer, now_string);
  g_free (now_string);

  g_string_append (vsx_log_buffer, "] ");

  va_start (ap, format);
  g_string_append_vprintf (vsx_log_buffer, format, ap);
  va_end (ap);

  g_string_append_c (vsx_log_buffer, '\n');

  g_cond_signal (&vsx_log_cond);

  g_mutex_unlock (&vsx_log_mutex);
}

static void
block_sigint (void)
{
  sigset_t sigset;

  sigemptyset (&sigset);
  sigaddset (&sigset, SIGINT);
  sigaddset (&sigset, SIGTERM);

  if (pthread_sigmask (SIG_BLOCK, &sigset, NULL) == -1)
    g_warning ("pthread_sigmask failed: %s", strerror (errno));
}

static gpointer
vsx_log_thread_func (gpointer data)
{
  GString *alternate_buffer;
  gboolean had_error = FALSE;
  GString *tmp;

  block_sigint ();

  alternate_buffer = g_string_new (NULL);

  g_mutex_lock (&vsx_log_mutex);

  while (!vsx_log_finished || vsx_log_buffer->len > 0)
    {
      size_t wrote;

      /* Wait until there's something to do */
      while (!vsx_log_finished && vsx_log_buffer->len == 0)
        g_cond_wait (&vsx_log_cond, &vsx_log_mutex);

      if (had_error)
        /* Just ignore the data */
        g_string_set_size (vsx_log_buffer, 0);
      else
        {
          /* Swap the log buffer for an empty alternate buffer so we can
             write from the normal one */
          tmp = vsx_log_buffer;
          vsx_log_buffer = alternate_buffer;
          alternate_buffer = tmp;

          /* Release the mutex while we do a blocking write */
          g_mutex_unlock (&vsx_log_mutex);

          wrote = fwrite (alternate_buffer->str,
                          1 /* size */,
                          alternate_buffer->len,
                          vsx_log_file);

          /* If there was an error then we'll just start ignoring data
             until we're told to quit */
          if (wrote != alternate_buffer->len)
            had_error = TRUE;
          else
            fflush (vsx_log_file);

          g_string_set_size (alternate_buffer, 0);

          g_mutex_lock (&vsx_log_mutex);
        }
    }

  g_mutex_unlock (&vsx_log_mutex);

  g_string_free (alternate_buffer, TRUE);

  return NULL;
}

gboolean
vsx_log_set_file (const char *filename,
                  GError **error)
{
  FILE *file;

  file = fopen (filename, "a");

  if (file == NULL)
    {
      g_set_error_literal (error,
                           G_FILE_ERROR,
                           g_file_error_from_errno (errno),
                           strerror (errno));
      return FALSE;
    }

  vsx_log_close ();

  vsx_log_file = file;
  vsx_log_buffer = g_string_new (NULL);
  vsx_log_finished = FALSE;

  return TRUE;
}

gboolean
vsx_log_start (GError **error)
{
  if (!vsx_log_available () || vsx_log_thread != NULL)
    return TRUE;

  vsx_log_thread = g_thread_try_new ("vsx-log",
                                     vsx_log_thread_func,
                                     NULL, /* data */
                                     error);

  return vsx_log_thread != NULL;
}

void
vsx_log_close (void)
{
  if (vsx_log_thread)
    {
      g_mutex_lock (&vsx_log_mutex);
      vsx_log_finished = TRUE;
      g_cond_signal (&vsx_log_cond);
      g_mutex_unlock (&vsx_log_mutex);

      g_thread_join (vsx_log_thread);

      vsx_log_thread = NULL;
    }

  if (vsx_log_buffer)
    {
      g_string_free (vsx_log_buffer, TRUE);
      vsx_log_buffer = NULL;
    }

  if (vsx_log_file)
    {
      fclose (vsx_log_file);
      vsx_log_file = NULL;
    }
}
