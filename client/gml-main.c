/*
 * Gemelo - A server for chatting with strangers in a foreign language
 * Copyright (C) 2012  Neil Roberts
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

#include "gml-connection.h"

static char *option_server_base_url = "http://www.gemelo.org:5142/";
static char *option_room = "english";

static GOptionEntry
options[] =
  {
    {
      "url", 'u', 0, G_OPTION_ARG_STRING, &option_server_base_url,
      "URL of the server", "url"
    },
    {
      "room", 'r', 0, G_OPTION_ARG_STRING, &option_room,
      "Room to connect to", "room"
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
  context = g_option_context_new ("- Chat to a random stranger!");
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

static void
connection_error_cb (GmlConnection *connection,
                     GError *error,
                     GMainLoop *main_loop)
{
  fprintf (stderr, "%s\n", error->message);
}

static void
running_cb (GmlConnection *connection,
            GParamSpec *pspec,
            GMainLoop *main_loop)
{
  if (!gml_connection_get_running (connection))
    g_main_loop_quit (main_loop);
}

static void
message_cb (GmlConnection *connection,
            GmlConnectionPerson person,
            const char *message)
{
  printf ("%s: %s\n",
          person == GML_CONNECTION_PERSON_YOU ? "you" : "stranger",
          message);
}

static void
stranger_typing_cb (GObject *object,
                    GParamSpec *pspec)
{
  GmlConnection *connection = GML_CONNECTION (object);

  if (gml_connection_get_stranger_typing (connection))
    printf ("typing\n");
  else
    printf ("not typing\n");
}

static void
state_cb (GObject *object,
          GParamSpec *pspec)
{
  GmlConnection *connection = GML_CONNECTION (object);

  switch (gml_connection_get_state (connection))
    {
    case GML_CONNECTION_STATE_AWAITING_PARTNER:
      printf ("awaiting partner\n");
      break;

    case GML_CONNECTION_STATE_IN_PROGRESS:
      printf ("in progress\n");
      break;

    case GML_CONNECTION_STATE_DONE:
      printf ("done\n");
      break;
    }
}

gboolean
message_timeout_cb (gpointer data)
{
  GmlConnection *connection = data;
  static int counter = 0;

  if (counter++ >= 10)
    {
      gml_connection_leave (connection);
      return FALSE;
    }
  else if (counter & 1)
    {
      gml_connection_set_typing (connection, TRUE);
      return TRUE;
    }
  else
    {
      char *message = g_strdup_printf ("message %i", counter);
      gml_connection_send_message (connection, message);
      gml_connection_set_typing (connection, FALSE);
      g_free (message);
      return TRUE;
    }
}

int
main (int argc, char **argv)
{
  GError *error = NULL;
  GmlConnection *connection;
  GMainLoop *main_loop;

  g_type_init ();

  if (!process_arguments (&argc, &argv, &error))
    {
      fprintf (stderr, "%s\n", error->message);
      return EXIT_FAILURE;
    }

  connection = gml_connection_new (option_server_base_url,
                                   option_room);

  main_loop = g_main_loop_new (NULL, FALSE);

  g_signal_connect (connection,
                    "got-error",
                    G_CALLBACK (connection_error_cb),
                    main_loop);
  g_signal_connect (connection,
                    "message",
                    G_CALLBACK (message_cb),
                    NULL);
  g_signal_connect (connection,
                    "notify::stranger-typing",
                    G_CALLBACK (stranger_typing_cb),
                    NULL);
  g_signal_connect (connection,
                    "notify::state",
                    G_CALLBACK (state_cb),
                    NULL);
  g_signal_connect (connection,
                    "notify::running",
                    G_CALLBACK (running_cb),
                    main_loop);

  gml_connection_send_message (connection, "hello");

  gml_connection_set_running (connection, TRUE);

  g_timeout_add_seconds (2, message_timeout_cb, connection);

  g_main_loop_run (main_loop);

  g_main_loop_unref (main_loop);

  g_object_unref (connection);

  return 0;
}
