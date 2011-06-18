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
#include <stdio.h>
#include <stdlib.h>

#include "gml-server.h"
#include "gml-main-context.h"
#include "gml-log.h"

static char *option_listen_address = "0.0.0.0";
static int option_listen_port = 5142;
static char *option_log_file = NULL;

static GOptionEntry
options[] =
  {
    {
      "address", 'a', 0, G_OPTION_ARG_STRING, &option_listen_address,
      "Address to listen on", "address"
    },
    {
      "port", 'p', 0, G_OPTION_ARG_INT, &option_listen_port,
      "Port to listen on", "port"
    },
    {
      "log", 'l', 0, G_OPTION_ARG_STRING, &option_log_file,
      "File to write log messages to", "file"
    },
    { NULL, 0, 0, 0, NULL, NULL, NULL }
  };

static gboolean
process_arguments (int *argc, char ***argv,
                   GError **error)
{
  GOptionContext *context;
  gboolean ret;
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
      ret = FALSE;
    }

  return ret;
}

static GmlServer *
create_server (GError **error)
{
  GInetAddress *inet_address;
  GmlServer *server = NULL;

  inet_address = g_inet_address_new_from_string (option_listen_address);

  if (inet_address == NULL)
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                 "Failed to parse address '%s'",
                 option_listen_address);
  else
    {
      GSocketAddress *address = g_inet_socket_address_new (inet_address,
                                                           option_listen_port);

      server = gml_server_new (address, error);

      g_object_unref (address);
      g_object_unref (inet_address);
    }

  return server;
}

int
main (int argc, char **argv)
{
  GError *error = NULL;
  GmlMainContext *mc;
  GmlServer *server;

  g_thread_init (NULL);
  g_type_init ();

  if (!process_arguments (&argc, &argv, &error))
    {
      fprintf (stderr, "%s\n", error->message);
      return EXIT_FAILURE;
    }

  mc = gml_main_context_get_default (&error);

  if (mc == NULL)
    {
      fprintf (stderr, "%s\n", error->message);
      g_clear_error (&error);
    }
  else
    {
      if (option_log_file
          && !gml_log_set_file (option_log_file, &error))
        {
          fprintf (stderr, "Error setting log file: %s\n", error->message);
          g_clear_error (&error);
        }
      else
        {
          server = create_server (&error);

          if (server == NULL)
            {
              fprintf (stderr, "%s\n", error->message);
              g_clear_error (&error);
            }
          else
            {
              gml_log ("Server listening on port %i", option_listen_port);

              if (!gml_server_run (server, &error))
                gml_log ("%s", error->message);

              gml_log ("Exiting...");

              gml_server_free (server);
            }

          gml_log_close ();
        }

      gml_main_context_free (mc);
    }

  return 0;
}
