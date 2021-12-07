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

#include "config.h"

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>

#include "vsx-log.h"
#include "vsx-util.h"
#include "vsx-buffer.h"
#include "vsx-file-error.h"

static FILE *vsx_log_file = NULL;
static struct vsx_buffer vsx_log_buffer = VSX_BUFFER_STATIC_INIT;
static pthread_t vsx_log_thread;
static bool vsx_log_has_thread = false;
static pthread_mutex_t vsx_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t vsx_log_cond = PTHREAD_COND_INITIALIZER;
static bool vsx_log_finished = false;

bool
vsx_log_available (void)
{
  return vsx_log_file != NULL;
}

void
vsx_log (const char *format,
         ...)
{
  va_list ap;

  if (!vsx_log_available ())
    return;

  pthread_mutex_lock (&vsx_log_mutex);

  time_t now;
  time (&now);
  struct tm tm;
  gmtime_r (&now, &tm);

  vsx_buffer_append_printf (&vsx_log_buffer,
                            "[%4d-%02d-%02dT%02d:%02d:%02dZ] ",
                            tm.tm_year + 1900,
                            tm.tm_mon + 1,
                            tm.tm_mday,
                            tm.tm_hour,
                            tm.tm_min,
                            tm.tm_sec);

  va_start (ap, format);
  vsx_buffer_append_vprintf (&vsx_log_buffer, format, ap);
  va_end (ap);

  vsx_buffer_append_c (&vsx_log_buffer, '\n');

  pthread_cond_signal (&vsx_log_cond);

  pthread_mutex_unlock (&vsx_log_mutex);
}

static void
block_sigint (void)
{
  sigset_t sigset;

  sigemptyset (&sigset);
  sigaddset (&sigset, SIGINT);
  sigaddset (&sigset, SIGTERM);

  if (pthread_sigmask (SIG_BLOCK, &sigset, NULL) == -1)
    vsx_warning ("pthread_sigmask failed: %s", strerror (errno));
}

static void *
vsx_log_thread_func (void *data)
{
  struct vsx_buffer alternate_buffer;
  struct vsx_buffer tmp;
  bool had_error = false;

  block_sigint ();

  vsx_buffer_init (&alternate_buffer);

  pthread_mutex_lock (&vsx_log_mutex);

  while (!vsx_log_finished || vsx_log_buffer.length > 0)
    {
      size_t wrote;

      /* Wait until there's something to do */
      while (!vsx_log_finished && vsx_log_buffer.length == 0)
        pthread_cond_wait (&vsx_log_cond, &vsx_log_mutex);

      if (had_error)
        /* Just ignore the data */
        vsx_buffer_set_length (&vsx_log_buffer, 0);
      else
        {
          /* Swap the log buffer for an empty alternate buffer so we can
             write from the normal one */
          tmp = vsx_log_buffer;
          vsx_log_buffer = alternate_buffer;
          alternate_buffer = tmp;

          /* Release the mutex while we do a blocking write */
          pthread_mutex_unlock (&vsx_log_mutex);

          wrote = fwrite (alternate_buffer.data,
                          1 /* size */,
                          alternate_buffer.length,
                          vsx_log_file);

          /* If there was an error then we'll just start ignoring data
             until we're told to quit */
          if (wrote != alternate_buffer.length)
            had_error = true;
          else
            fflush (vsx_log_file);

          vsx_buffer_set_length (&alternate_buffer, 0);

          pthread_mutex_lock (&vsx_log_mutex);
        }
    }

  pthread_mutex_unlock (&vsx_log_mutex);

  vsx_buffer_destroy (&alternate_buffer);

  return NULL;
}

bool
vsx_log_set_file (const char *filename,
                  struct vsx_error **error)
{
  FILE *file;

  file = fopen (filename, "a");

  if (file == NULL)
    {
      vsx_file_error_set (error,
                          errno,
                          "%s: %s",
                          filename,
                          strerror (errno));
      return false;
    }

  vsx_log_close ();

  vsx_log_file = file;
  vsx_log_finished = false;

  return true;
}

void
vsx_log_start (void)
{
  if (!vsx_log_available ())
    return;

  if (vsx_log_has_thread)
    return;

  int res = pthread_create (&vsx_log_thread,
                            NULL, /* attr */
                            vsx_log_thread_func,
                            NULL /* thread func arg */);

  if (res)
      vsx_fatal ("Error creating thread: %s", strerror (res));

  vsx_log_has_thread = true;
}

void
vsx_log_close (void)
{
  if (vsx_log_has_thread)
    {
      pthread_mutex_lock (&vsx_log_mutex);
      vsx_log_finished = true;
      pthread_cond_signal (&vsx_log_cond);
      pthread_mutex_unlock (&vsx_log_mutex);

      pthread_join (vsx_log_thread, NULL);

      vsx_log_has_thread = false;
    }

  vsx_buffer_destroy (&vsx_log_buffer);
  vsx_buffer_init (&vsx_log_buffer);

  if (vsx_log_file)
    {
      fclose (vsx_log_file);
      vsx_log_file = NULL;
    }
}
