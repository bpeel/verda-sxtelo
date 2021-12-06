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

#ifdef USE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

#include "vsx-server.h"
#include "vsx-main-context.h"
#include "vsx-log.h"
#include "vsx-config.h"
#include "vsx-buffer.h"

static char *option_log_file = NULL;
static char *option_config_file = NULL;
static bool option_daemonize = false;
static char *option_user = NULL;
static char *option_group = NULL;

static GOptionEntry
options[] =
  {
    {
      "config", 'c', 0, G_OPTION_ARG_STRING, &option_config_file,
      "Config file to use instead of the default", "file"
    },
    {
      "log", 'l', 0, G_OPTION_ARG_STRING, &option_log_file,
      "File to write log messages to", "file"
    },
    {
      "daemonize", 'd', 0, G_OPTION_ARG_NONE, &option_daemonize,
      "Launch the server in a separate detached process", NULL
    },
    {
      "user", 'u', 0, G_OPTION_ARG_STRING, &option_user,
      "Run the daemon as USER", "USER"
    },
    {
      "group", 'g', 0, G_OPTION_ARG_STRING, &option_group,
      "Run the daemon as GROUP", "GROUP"
    },
    { NULL, 0, 0, 0, NULL, NULL, NULL }
  };

static bool
process_arguments (int *argc, char ***argv,
                   GError **error)
{
  GOptionContext *context;
  bool ret;
  GOptionGroup *group;

  group = g_option_group_new (NULL, /* name */
                              NULL, /* description */
                              NULL, /* help_description */
                              NULL, /* user_data */
                              NULL /* destroy notify */);
  g_option_group_add_entries (group, options);
  context = g_option_context_new ("- A server for practicing a "
                                  "foreign language");
  g_option_context_set_main_group (context, group);
  ret = g_option_context_parse (context, argc, argv, error);
  g_option_context_free (context);

  if (ret && *argc > 1)
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_UNKNOWN_OPTION,
                   "Unknown option '%s'", (* argv)[1]);
      ret = false;
    }

  return ret;
}

static VsxConfig *
load_config(GError **error)
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

  g_set_error (error,
               G_FILE_ERROR,
               G_FILE_ERROR_NOENT,
               "No config file found");

 found:
  vsx_buffer_destroy (&filename);

  return config;
}

static VsxServer *
create_server (VsxConfig *config,
               GError **error)
{
  g_assert (!vsx_list_empty (&config->servers));

  int override_fd = -1;

#ifdef USE_SYSTEMD
  {
    int nfds = sd_listen_fds (true /* unset_environment */);

    if (nfds < 0)
      {
        g_set_error (error,
                     G_FILE_ERROR,
                     g_file_error_from_errno (-nfds),
                     "Error getting systemd fds: %s",
                     strerror (-nfds));
        return NULL;
      }
    if (nfds > 0)
      {
        if (nfds != vsx_list_length (&config->servers))
          {
            g_set_error (error,
                         G_FILE_ERROR,
                         G_FILE_ERROR_BADF,
                         "Wrong number of file descriptors received from "
                         "systemd (expected: %i, got %i)",
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
  GError *error = NULL;
  VsxMainContext *mc;
  VsxServer *server;
  VsxConfig *config;

  if (!process_arguments (&argc, &argv, &error))
    {
      fprintf (stderr, "%s\n", error->message);
      return EXIT_FAILURE;
    }

  config = load_config (&error);

  if (config == NULL)
    {
      fprintf (stderr, "%s\n", error->message);
      g_clear_error (&error);
      return EXIT_FAILURE;
    }

  mc = vsx_main_context_get_default (&error);

  if (mc == NULL)
    {
      fprintf (stderr, "%s\n", error->message);
      g_clear_error (&error);
    }
  else
    {
      struct vsx_error *log_error = NULL;

      const char *log_file = (option_log_file ?
                              option_log_file :
                              config->log_file);

      if (log_file && !vsx_log_set_file (log_file, &log_error))
        {
          fprintf (stderr, "Error setting log file: %s\n", log_error->message);
          vsx_error_free (log_error);
        }
      else
        {
          server = create_server (config, &error);

          if (server == NULL)
            {
              fprintf (stderr, "%s\n", error->message);
              g_clear_error (&error);
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
                vsx_log ("%s", error->message);

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
