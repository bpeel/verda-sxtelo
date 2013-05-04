/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2012, 2013  Neil Roberts
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
#include <stdarg.h>
#include <readline/readline.h>
#include <term.h>

#include "vsx-connection.h"

static void
format_print (const char *format, ...);

static char *option_server_base_url = "http://vs.busydoingnothing.co.uk:5142/";
static char *option_room = "default";
static char *option_player_name = NULL;
static gboolean option_debug = FALSE;

static VsxConnection *connection;
static GMainLoop *main_loop;
static GSource *stdin_source = NULL;

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
    {
      "player-name", 'p', 0, G_OPTION_ARG_STRING, &option_player_name,
      "Name of the player", "player"
    },
    {
      "debug", 'd', 0, G_OPTION_ARG_NONE, &option_debug,
      "Enable HTTP debugging", NULL
    },
    { NULL, 0, 0, 0, NULL, NULL, NULL }
  };

static const char typing_prompt[] = "vs*> ";
static const char not_typing_prompt[] = "vs > ";

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
connection_error_cb (VsxConnection *connection,
                     GError *error)
{
  format_print ("error: %s\n", error->message);
}

static void
running_cb (VsxConnection *connection,
            GParamSpec *pspec)
{
  if (!vsx_connection_get_running (connection))
    g_main_loop_quit (main_loop);
}

static void
message_cb (VsxConnection *connection,
            const VsxPlayer *player,
            const char *message)
{
  format_print ("%s: %s\n",
                vsx_player_get_name (player),
                message);
}

static void
output_ti (char *name)
{
  const char *s;

  s = tigetstr (name);
  if (s && s != (char *) -1)
    fputs (s, stdout);
}

static void
clear_line (void)
{
  output_ti ("cr");
  output_ti ("dl1");
}

typedef struct
{
  const VsxPlayer *self;
  gboolean is_typing;
} CheckTypingData;

static void
check_typing_cb (const VsxPlayer *player,
                 void *user_data)
{
  CheckTypingData *data = user_data;

  if (player != data->self && vsx_player_is_typing (player))
    data->is_typing = TRUE;
}

static void
player_changed_cb (VsxConnection *connection,
                   const VsxPlayer *player)
{
  CheckTypingData data;

  data.self = vsx_connection_get_self (connection);
  data.is_typing = FALSE;

  vsx_connection_foreach_player (connection, check_typing_cb, &data);

  clear_line ();
  rl_set_prompt (data.is_typing ? typing_prompt : not_typing_prompt);
  rl_forced_update_display ();
}

static void
tile_changed_cb (VsxConnection *connection,
                 gboolean is_new,
                 const VsxTile *tile)
{
  char letter[7];
  int letter_len;

  letter_len = g_unichar_to_utf8 (vsx_tile_get_letter (tile), letter);
  letter[letter_len] = '\0';

  format_print ("%s: %i (%i,%i) %s\n",
                is_new ? "new_tile" : "tile changed",
                vsx_tile_get_number (tile),
                vsx_tile_get_x (tile),
                vsx_tile_get_y (tile),
                letter);
}

static void
print_state_message (VsxConnection *connection)
{
  switch (vsx_connection_get_state (connection))
    {
    case VSX_CONNECTION_STATE_AWAITING_HEADER:
      break;

    case VSX_CONNECTION_STATE_IN_PROGRESS:
      format_print ("You are now in a conversation with a stranger. Say hi!\n");
      break;

    case VSX_CONNECTION_STATE_DONE:
      format_print ("The conversation has finished\n");
      break;
    }
}

static void
state_cb (GObject *object,
          GParamSpec *pspec)
{
  VsxConnection *connection = VSX_CONNECTION (object);

  print_state_message (connection);
}

static void
remove_stdin_source (void)
{
  if (stdin_source)
    {
      clear_line ();
      rl_callback_handler_remove ();
      g_source_destroy (stdin_source);
      stdin_source = NULL;
    }
}

static gboolean
stdin_cb (gpointer user_data)
{
  rl_callback_read_char ();

  return TRUE;
}

static void
readline_cb (char *line)
{
  if (line == NULL)
    {
      remove_stdin_source ();

      if (vsx_connection_get_state (connection)
          == VSX_CONNECTION_STATE_IN_PROGRESS)
        vsx_connection_leave (connection);
      else
        g_main_loop_quit (main_loop);
    }
}

static void
format_print (const char *format, ...)
{
  va_list ap;

  va_start (ap, format);

  clear_line ();

  vfprintf (stdout, format, ap);

  if (stdin_source)
    rl_forced_update_display ();

  va_end (ap);
}

static int
newline_cb (int count, int key)
{
  if (*rl_line_buffer)
    {
      vsx_connection_send_message (connection, rl_line_buffer);
      rl_replace_line ("", TRUE);
    }

  return 0;
}

static void
redisplay_hook (void)
{
  /* There doesn't appear to be a good way to hook into notifications
     of the buffer being modified so we'll just hook into the
     redisplay function which should hopefully get called every time
     it is modified. If the buffer is not empty then we'll assume the
     user is typing. If the user is already marked as typing then this
     will do nothing */
  vsx_connection_set_typing (connection, *rl_line_buffer != '\0');

  /* Chain up */
  rl_redisplay ();
}

static void
make_stdin_source (void)
{
  GIOChannel *io_channel;

  rl_callback_handler_install (not_typing_prompt, readline_cb);
  rl_redisplay_function = redisplay_hook;
  rl_bind_key ('\r', newline_cb);

  io_channel = g_io_channel_unix_new (STDIN_FILENO);
  stdin_source = g_io_create_watch (io_channel, G_IO_IN);
  g_io_channel_unref (io_channel);

  g_source_set_callback (stdin_source,
                         stdin_cb,
                         NULL /* data */,
                         NULL /* notify */);

  g_source_attach (stdin_source, NULL);
}

static void
logging_printer_cb (SoupLogger *logger,
                    SoupLoggerLogLevel level,
                    char direction,
                    const char *data,
                    gpointer user_data)
{
  format_print ("%c %s\n", direction, data);
}

static void
add_logging_feature (SoupSession *session)
{
  SoupLogger *logger = soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);

  soup_logger_set_printer (logger,
                           logging_printer_cb,
                           NULL, /* user_data */
                           NULL /* destroy */);

  soup_session_add_feature (session, SOUP_SESSION_FEATURE (logger));

  g_object_unref (logger);
}

int
main (int argc, char **argv)
{
  GError *error = NULL;
  SoupSession *soup_session;

  g_type_init ();

  if (!process_arguments (&argc, &argv, &error))
    {
      fprintf (stderr, "%s\n", error->message);
      return EXIT_FAILURE;
    }

  soup_session = soup_session_async_new ();

  if (option_debug)
    add_logging_feature (soup_session);

  make_stdin_source ();

  if (option_player_name == NULL)
    option_player_name = g_strdup (g_get_user_name ());

  connection = vsx_connection_new (soup_session,
                                   option_server_base_url,
                                   option_room,
                                   option_player_name);

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
                    "player-changed",
                    G_CALLBACK (player_changed_cb),
                    NULL);
  g_signal_connect (connection,
                    "tile-changed",
                    G_CALLBACK (tile_changed_cb),
                    NULL);
  g_signal_connect (connection,
                    "notify::state",
                    G_CALLBACK (state_cb),
                    NULL);
  g_signal_connect (connection,
                    "notify::running",
                    G_CALLBACK (running_cb),
                    main_loop);

  vsx_connection_set_running (connection, TRUE);

  print_state_message (connection);

  g_main_loop_run (main_loop);

  g_main_loop_unref (main_loop);

  remove_stdin_source ();

  g_object_unref (connection);

  g_object_unref (soup_session);

  return 0;
}
