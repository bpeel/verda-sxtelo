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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <grp.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/types.h>

#ifdef USE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

#include "vsx-server.h"
#include "vsx-main-context.h"
#include "vsx-log.h"
#include "vsx-config.h"
#include "vsx-buffer.h"
#include "vsx-file-error.h"

static char *option_log_file = NULL;
static char *option_config_file = NULL;
static bool option_daemonize = false;
static char *option_user = NULL;
static char *option_group = NULL;

static const char options[] = "-hl:c:du:g:";

static void
usage (void)
{
  printf ("verda-sxtelo - An anagram game in Esperanto for the web\n"
          "usage: verda-sxtelo [options]...\n"
          " -h                   Show this help message\n"
          " -c <file>            Specify a config file to use instead of\n"
          "                      the default.\n"
          " -l <file>            File to write log messages to.\n"
          " -d                   Fork and detach from terminal\n"
          "                      (Daemonize)\n"
          " -u <user>            Drop privileges to user\n"
          " -g <group>           Drop privileges to group\n");
}

static bool
process_arguments(int argc, char **argv)
{
  int opt;

  opterr = false;

  while ((opt = getopt (argc, argv, options)) != -1) {
    switch (opt) {
    case ':':
    case '?':
      fprintf (stderr,
               "invalid option '%c'\n",
               optopt);
      return false;

    case '\1':
      fprintf (stderr,
               "unexpected argument \"%s\"\n",
               optarg);
      return false;

    case 'h':
      usage ();
      return false;

    case 'l':
      option_log_file = optarg;
      break;

    case 'c':
      option_config_file = optarg;
      break;

    case 'd':
      option_daemonize =  true;
      break;

    case 'u':
      option_user = optarg;
      break;

    case 'g':
      option_group = optarg;
      break;
    }
  }

  return true;
}

static VsxConfig *
load_config(struct vsx_error **error)
{
  if (option_config_file)
    return vsx_config_load (option_config_file, error);

  const char * const *dirs = g_get_system_config_dirs ();
  struct vsx_buffer filename = VSX_BUFFER_STATIC_INIT;
  VsxConfig *config = NULL;

  for (int i = 0; dirs[i]; i++)
    {
      vsx_buffer_set_length (&filename, 0);
      vsx_buffer_append_string (&filename, dirs[i]);
      vsx_buffer_append_string (&filename,
                                G_DIR_SEPARATOR_S
                                "verda-sxtelo"
                                G_DIR_SEPARATOR_S
                                "conf.txt");

      if (g_file_test ((const char *) filename.data, G_FILE_TEST_EXISTS))
        {
          config = vsx_config_load ((const char *) filename.data, error);
          goto found;
        }
    }

  vsx_set_error (error,
                 &vsx_file_error,
                 VSX_FILE_ERROR_NOENT,
                 "No config file found");

 found:
  vsx_buffer_destroy (&filename);

  return config;
}

static VsxServer *
create_server (VsxConfig *config,
               struct vsx_error **error)
{
  g_assert (!vsx_list_empty (&config->servers));

  int override_fd = -1;

#ifdef USE_SYSTEMD
  {
    int nfds = sd_listen_fds (true /* unset_environment */);

    if (nfds < 0)
      {
        vsx_file_error_set (error,
                            -nfds,
                            "Error getting systemd fds: %s",
                            strerror (-nfds));
        return NULL;
      }
    if (nfds > 0)
      {
        if (nfds != vsx_list_length (&config->servers))
          {
            vsx_set_error (error,
                           &vsx_file_error,
                           VSX_FILE_ERROR_BADF,
                           "Wrong number of file descriptors received "
                           "from systemd (expected: %i, got %i)",
                           vsx_list_length (&config->servers),
                           nfds);
            return NULL;
          }

        override_fd = SD_LISTEN_FDS_START;
      }
  }
#endif /* USE_SYSTEMD */

  VsxServer *server = vsx_server_new ();

  VsxConfigServer *server_config;

  vsx_list_for_each (server_config, &config->servers, link)
    {
      if (!vsx_server_add_config (server, server_config, override_fd, error))
        {
          vsx_server_free (server);
          return NULL;
        }

      if (override_fd != -1)
        override_fd++;
    }

  return server;
}

static void
daemonize (void)
{
  pid_t pid, sid;

  pid = fork ();

  if (pid < 0)
    {
      g_warning ("fork failed: %s", strerror (errno));
      exit (EXIT_FAILURE);
    }
  if (pid > 0)
    /* Parent process, we can just quit */
    exit (EXIT_SUCCESS);

  /* Reset the file mask (not really sure why we do this..) */
  umask (0);

  /* Create a new SID for the child process */
  sid = setsid ();
  if (sid < 0)
    {
      g_warning ("setsid failed: %s", strerror (errno));
      exit (EXIT_FAILURE);
    }

  /* Change the working directory so we're resilient against it being
     removed */
  if (chdir ("/") < 0)
    {
      g_warning ("chdir failed: %s", strerror (errno));
      exit (EXIT_FAILURE);
    }

  /* Redirect standard files to /dev/null */
  freopen ("/dev/null", "r", stdin);
  freopen ("/dev/null", "w", stdout);
  freopen ("/dev/null", "w", stderr);
}

static void
set_user (const char *user_name)
{
  struct passwd *user_info;

  user_info = getpwnam (user_name);

  if (user_info == NULL)
    {
      fprintf (stderr, "Unknown user \"%s\"\n", user_name);
      exit (EXIT_FAILURE);
    }

  if (setuid (user_info->pw_uid) == -1)
    {
      fprintf (stderr, "Error setting user privileges: %s\n",
               strerror (errno));
      exit (EXIT_FAILURE);
    }
}

static void
set_group (const char *group_name)
{
  struct group *group_info;

  group_info = getgrnam (group_name);

  if (group_info == NULL)
    {
      fprintf (stderr, "Unknown group \"%s\"\n", group_name);
      exit (EXIT_FAILURE);
    }

  if (setgid (group_info->gr_gid) == -1)
    {
      fprintf (stderr, "Error setting group privileges: %s\n",
               strerror (errno));
      exit (EXIT_FAILURE);
    }
}

int
main (int argc, char **argv)
{
  VsxMainContext *mc;
  VsxServer *server;
  VsxConfig *config;

  if (!process_arguments (argc, argv))
    return EXIT_FAILURE;

  struct vsx_error *error = NULL;

  config = load_config (&error);

  if (config == NULL)
    {
      fprintf (stderr, "%s\n", error->message);
      vsx_error_free (error);
      return EXIT_FAILURE;
    }

  mc = vsx_main_context_get_default (&error);

  if (mc == NULL)
    {
      fprintf (stderr, "%s\n", error->message);
      vsx_error_free (error);
    }
  else
    {
      const char *log_file = (option_log_file ?
                              option_log_file :
                              config->log_file);

      if (log_file && !vsx_log_set_file (log_file, &error))
        {
          fprintf (stderr, "Error setting log file: %s\n", error->message);
          vsx_error_free (error);
        }
      else
        {
          server = create_server (config, &error);

          if (server == NULL)
            {
              fprintf (stderr, "%s\n", error->message);
              vsx_error_free (error);
            }
          else
            {
              const char *group = option_group ? option_group : config->group;

              if (group)
                set_group (group);

              const char *user = option_user ? option_user : config->user;

              if (user)
                set_user (user);

              if (option_daemonize)
                daemonize ();

              vsx_log_start ();

              if (!vsx_server_run (server, &error))
                {
                  vsx_log ("%s", error->message);
                  vsx_error_free (error);
                }

              vsx_log ("Exiting...");

              vsx_server_free (server);
            }

          vsx_log_close ();
        }

      vsx_main_context_free (mc);
    }

  vsx_config_free (config);

  return 0;
}
