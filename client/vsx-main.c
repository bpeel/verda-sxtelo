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

#include "config.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <readline/readline.h>
#include <term.h>

#include "vsx-connection.h"
#include "vsx-utf8.h"

static void
format_print (const char *format, ...);

static char *option_server = "gemelo.org";
int option_server_port = 5144;
static char *option_room = "default";
static char *option_player_name = NULL;
static bool option_debug = false;

static VsxConnection *connection;
static GMainLoop *main_loop;
static GSource *stdin_source = NULL;

static GOptionEntry
options[] =
  {
    {
      "server", 's', 0, G_OPTION_ARG_STRING, &option_server,
      "Hostname of the server", "host"
    },
    {
      "server-port", 'p', 0, G_OPTION_ARG_INT, &option_server_port,
      "Port to connect to on the server", "port"
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
  context = g_option_context_new ("- Chat to a random stranger!");
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

static void
handle_error (VsxConnection *connection,
              const VsxConnectionEvent *event)
{
  format_print ("error: %s\n", event->error.error->message);
}

static void
handle_running_state_changed (VsxConnection *connection,
                              const VsxConnectionEvent *event)
{
  if (!event->running_state_changed.running)
    g_main_loop_quit (main_loop);
}

static void
handle_message (VsxConnection *connection,
                const VsxConnectionEvent *event)
{
  format_print ("%s: %s\n",
                vsx_player_get_name (event->message.player),
                event->message.message);
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
  bool is_typing;
} CheckTypingData;

static void
check_typing_cb (const VsxPlayer *player,
                 void *user_data)
{
  CheckTypingData *data = user_data;

  if (player != data->self && vsx_player_is_typing (player))
    data->is_typing = true;
}

static void
handle_player_changed (VsxConnection *connection,
                       const VsxConnectionEvent *event)
{
  CheckTypingData data;

  data.self = vsx_connection_get_self (connection);
  data.is_typing = false;

  vsx_connection_foreach_player (connection, check_typing_cb, &data);

  clear_line ();
  rl_set_prompt (data.is_typing ? typing_prompt : not_typing_prompt);
  rl_forced_update_display ();
}

static void
handle_player_shouted (VsxConnection *connection,
                       const VsxConnectionEvent *event)
{

  const VsxPlayer *player = event->player_shouted.player;

  format_print ("** %s SHOUTS\n",
                vsx_player_get_name (player));
}

static void
handle_tile_changed (VsxConnection *connection,
                     const VsxConnectionEvent *event)
{
  char letter[7];
  int letter_len;

  const VsxTile *tile = event->tile_changed.tile;

  letter_len = vsx_utf8_encode (vsx_tile_get_letter (tile), letter);
  letter[letter_len] = '\0';

  format_print ("%s: %i (%i,%i) %s\n",
                event->tile_changed.new_tile ? "new_tile" : "tile changed",
                vsx_tile_get_number (tile),
                vsx_tile_get_x (tile),
                vsx_tile_get_y (tile),
                letter);
}

static void
event_cb (VsxListener *listener,
          void *data)
{
  const VsxConnectionEvent *event = data;

  switch (event->type)
    {
    case VSX_CONNECTION_EVENT_TYPE_ERROR:
      handle_error (connection, event);
      break;
    case VSX_CONNECTION_EVENT_TYPE_MESSAGE:
      handle_message (connection, event);
      break;
    case VSX_CONNECTION_EVENT_TYPE_PLAYER_CHANGED:
      handle_player_changed (connection, event);
      break;
    case VSX_CONNECTION_EVENT_TYPE_PLAYER_SHOUTED:
      handle_player_shouted (connection, event);
      break;
    case VSX_CONNECTION_EVENT_TYPE_TILE_CHANGED:
      handle_tile_changed (connection, event);
      break;
    case VSX_CONNECTION_EVENT_TYPE_RUNNING_STATE_CHANGED:
      handle_running_state_changed (connection, event);
      break;
    }
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

  return true;
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
      if (!strcmp (rl_line_buffer, "s"))
        vsx_connection_shout (connection);
      else if (!strcmp (rl_line_buffer, "t"))
        vsx_connection_turn (connection);
      else if (!strcmp (rl_line_buffer, "m"))
        vsx_connection_move_tile (connection, 0, 10, 20);
      else
        vsx_connection_send_message (connection, rl_line_buffer);

      rl_replace_line ("", true);
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

static GSocketAddress *
get_socket_address (GError **error)
{
  GResolver *resolver = g_resolver_get_default ();

  GList *addresses = g_resolver_lookup_by_name (resolver,
                                                option_server,
                                                NULL, /* cancellable */
                                                error);

  GSocketAddress *address;

  if (addresses)
    {
      address = g_inet_socket_address_new (addresses->data,
                                           option_server_port);
      g_resolver_free_addresses (addresses);
    }
  else
    {
      address = NULL;
    }

  g_object_unref (resolver);

  return address;
}

int
main (int argc, char **argv)
{
  GError *error = NULL;

  if (!process_arguments (&argc, &argv, &error))
    {
      fprintf (stderr, "%s\n", error->message);
      return EXIT_FAILURE;
    }

  GSocketAddress *server_address = get_socket_address (&error);

  if (server_address == NULL)
    {
      fprintf (stderr, "%s\n", error->message);
      return EXIT_FAILURE;
    }

  make_stdin_source ();

  if (option_player_name == NULL)
    option_player_name = g_strdup (g_get_user_name ());

  connection = vsx_connection_new (server_address,
                                   option_room,
                                   option_player_name);

  g_object_unref (server_address);

  main_loop = g_main_loop_new (NULL, false);

  VsxSignal *event_signal = vsx_connection_get_event_signal (connection);
  VsxListener event_listener =
    {
      .notify = event_cb,
    };

  vsx_signal_add (event_signal, &event_listener);

  g_signal_connect (connection,
                    "notify::state",
                    G_CALLBACK (state_cb),
                    NULL);

  vsx_connection_set_running (connection, true);

  print_state_message (connection);

  g_main_loop_run (main_loop);

  g_main_loop_unref (main_loop);

  remove_stdin_source ();

  g_object_unref (connection);

  return 0;
}
