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
#include <stdio.h>

#include "gml-log.h"

static FILE *gml_log_file = NULL;
static GString *gml_log_buffer = NULL;
static GThread *gml_log_thread = NULL;
static GMutex *gml_log_mutex = NULL;
static GCond *gml_log_cond = NULL;
static gboolean gml_log_finished = FALSE;

gboolean
gml_log_available (void)
{
  return gml_log_mutex != NULL;
}

void
gml_log (const char *format,
         ...)
{
  va_list ap;
  GTimeVal now;
  char *now_string;

  if (!gml_log_available ())
    return;

  g_mutex_lock (gml_log_mutex);

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

  g_cond_signal (gml_log_cond);

  g_mutex_unlock (gml_log_mutex);
}

static void
block_sigint (void)
{
  sigset_t sigset;

  sigemptyset (&sigset);
  sigaddset (&sigset, SIGINT);

  if (pthread_sigmask (SIG_BLOCK, &sigset, NULL) == -1)
    g_warning ("pthread_sigmask failed: %s", strerror (errno));
}

static gpointer
gml_log_thread_func (gpointer data)
{
  GString *alternate_buffer;
  gboolean had_error = FALSE;
  GString *tmp;

  block_sigint ();

  alternate_buffer = g_string_new (NULL);

  g_mutex_lock (gml_log_mutex);

  while (!gml_log_finished || gml_log_buffer->len > 0)
    {
      size_t wrote;

      /* Wait until there's something to do */
      while (!gml_log_finished && gml_log_buffer->len == 0)
        g_cond_wait (gml_log_cond, gml_log_mutex);

      if (had_error)
        /* Just ignore the data */
        g_string_set_size (gml_log_buffer, 0);
      else
        {
          /* Swap the log buffer for an empty alternate buffer so we can
             write from the normal one */
          tmp = gml_log_buffer;
          gml_log_buffer = alternate_buffer;
          alternate_buffer = tmp;

          /* Release the mutex while we do a blocking write */
          g_mutex_unlock (gml_log_mutex);

          wrote = fwrite (alternate_buffer->str,
                          1 /* size */,
                          alternate_buffer->len,
                          gml_log_file);

          /* If there was an error then we'll just start ignoring data
             until we're told to quit */
          if (wrote != alternate_buffer->len)
            had_error = TRUE;
          else
            fflush (gml_log_file);

          g_string_set_size (alternate_buffer, 0);

          g_mutex_lock (gml_log_mutex);
        }
    }

  g_mutex_unlock (gml_log_mutex);

  g_string_free (alternate_buffer, TRUE);

  return NULL;
}

gboolean
gml_log_set_file (const char *filename,
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

  gml_log_close ();

  gml_log_file = file;
  gml_log_buffer = g_string_new (NULL);
  gml_log_mutex = g_mutex_new ();
  gml_log_cond = g_cond_new ();
  gml_log_finished = FALSE;

  return TRUE;
}

gboolean
gml_log_start (GError **error)
{
  if (!gml_log_available () || gml_log_thread != NULL)
    return TRUE;

  gml_log_thread = g_thread_create (gml_log_thread_func,
                                    NULL, /* data */
                                    TRUE, /* joinable */
                                    error);

  if (gml_log_thread == NULL)
    gml_log_close ();

  return TRUE;
}

void
gml_log_close (void)
{
  if (gml_log_thread)
    {
      g_mutex_lock (gml_log_mutex);
      gml_log_finished = TRUE;
      g_cond_signal (gml_log_cond);
      g_mutex_unlock (gml_log_mutex);

      g_thread_join (gml_log_thread);

      gml_log_thread = NULL;
    }

  if (gml_log_cond)
    {
      g_cond_free (gml_log_cond);
      gml_log_cond = NULL;
    }

  if (gml_log_mutex)
    {
      g_mutex_free (gml_log_mutex);
      gml_log_mutex = NULL;
    }

  if (gml_log_buffer)
    {
      g_string_free (gml_log_buffer, TRUE);
      gml_log_buffer = NULL;
    }

  if (gml_log_file)
    {
      fclose (gml_log_file);
      gml_log_file = NULL;
    }
}
