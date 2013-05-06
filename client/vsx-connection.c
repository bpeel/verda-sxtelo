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

#include <glib-object.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <string.h>
#include <stdarg.h>

#include "vsx-connection.h"
#include "vsx-marshal.h"
#include "vsx-enum-types.h"
#include "vsx-player-private.h"
#include "vsx-tile-private.h"

enum
{
  PROP_0,

  PROP_SERVER_BASE_URL,
  PROP_SOUP_SESSION,
  PROP_ROOM,
  PROP_PLAYER_NAME,
  PROP_RUNNING,
  PROP_TYPING,
  PROP_STATE
};

enum
{
  SIGNAL_GOT_ERROR,
  SIGNAL_MESSAGE,
  SIGNAL_PLAYER_CHANGED,
  SIGNAL_PLAYER_SHOUTED,
  SIGNAL_TILE_CHANGED,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

static void
vsx_connection_dispose (GObject *object);

static void
vsx_connection_finalize (GObject *object);

static void
vsx_connection_queue_message (VsxConnection *connection);

static void
vsx_connection_maybe_send_command (VsxConnection *connection);

G_DEFINE_TYPE (VsxConnection, vsx_connection, G_TYPE_OBJECT);

#define VSX_CONNECTION_GET_PRIVATE(obj) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), VSX_TYPE_CONNECTION, \
                                VsxConnectionPrivate))

/* Initial timeout (in seconds) before attempting to reconnect after
   an error. The timeout will be doubled every time there is a
   failure */
#define VSX_CONNECTION_INITIAL_TIMEOUT 16

/* If the timeout reaches this maximum then it won't be doubled further */
#define VSX_CONNECTION_MAX_TIMEOUT 512

/* Time in seconds after the last message before sending a keep alive
   message (2.5 minutes) */
#define VSX_CONNECTION_KEEP_ALIVE_TIME 150

typedef enum
{
  VSX_CONNECTION_RUNNING_STATE_DISCONNECTED,
  VSX_CONNECTION_RUNNING_STATE_RUNNING,
  VSX_CONNECTION_RUNNING_STATE_WAITING_FOR_RECONNECT
} VsxConnectionRunningState;

struct _VsxConnectionPrivate
{
  char *server_base_url;
  char *room;
  char *player_name;
  SoupSession *soup_session;
  SoupMessage *message;
  GString *line_buffer;
  gboolean has_disconnected;
  JsonParser *json_parser;
  guint reconnect_timeout;
  guint reconnect_handler;
  VsxPlayer *self;
  char *person_id;
  VsxConnectionRunningState running_state;
  VsxConnectionState state;
  gboolean typing;
  gboolean sent_typing_state;
  int next_message_num;
  GQueue command_queue;
  SoupMessage *command_message;

  GHashTable *players;
  GHashTable *tiles;

  /* A timeout for sending a keep alive message */
  guint keep_alive_timeout;
  GTimer *keep_alive_time;
};

typedef enum
{
  VSX_CONNECTION_COMMAND_MESSAGE,
  VSX_CONNECTION_COMMAND_LEAVE,
  VSX_CONNECTION_COMMAND_SHOUT,
  VSX_CONNECTION_COMMAND_TURN,
  VSX_CONNECTION_COMMAND_MOVE_TILE
} VsxConnectionCommandType;

typedef struct
{
  VsxConnectionCommandType type;
  GList node;
} VsxConnectionCommand;

typedef struct
{
  VsxConnectionCommand parent;

  /* Over-allocated */
  char text[1];
} VsxConnectionMessageCommand;

typedef struct
{
  VsxConnectionCommand parent;

  int tile_num;
  int x, y;
} VsxConnectionMoveTileCommand;

static gboolean
vsx_connection_keep_alive_cb (void *data)
{
  VsxConnection *connection = data;
  VsxConnectionPrivate *priv = connection->priv;

  priv->keep_alive_timeout = 0;

  vsx_connection_maybe_send_command (connection);

  return FALSE;
}

static void
vsx_connection_queue_keep_alive (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  if (priv->keep_alive_timeout)
    g_source_remove (priv->keep_alive_timeout);

  priv->keep_alive_timeout
    = g_timeout_add_seconds (VSX_CONNECTION_KEEP_ALIVE_TIME + 1,
                             vsx_connection_keep_alive_cb,
                             connection);

  g_timer_start (priv->keep_alive_time);
}

static VsxConnectionCommand *
vsx_connection_pop_command (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  return g_queue_pop_head_link (&priv->command_queue)->data;
}

static SoupMessage *
vsx_connection_make_message (VsxConnection *connection,
                             const char *http_method,
                             const char *method,
                             const char *template,
                             ...)
{
  VsxConnectionPrivate *priv = connection->priv;
  SoupMessage *msg;
  GString *uri;
  va_list ap;
  int arg;

  uri = g_string_new (priv->server_base_url);

  if (uri->len == 0 || uri->str[uri->len - 1] != '/')
    g_string_append_c (uri, '/');

  g_string_append (uri, method);
  g_string_append_c (uri, '?');

  va_start (ap, template);

  for (arg = 0; template[arg]; arg++)
    {
      if (arg > 0)
        g_string_append_c (uri, '&');

      switch (template[arg])
        {
        case 's':
          {
            char *encoded_param =
              soup_uri_encode (va_arg (ap, char *), "&");
            g_string_append (uri, encoded_param);
            g_free (encoded_param);
          }
          break;

        case 'i':
          g_string_append_printf (uri, "%i", va_arg (ap, int));
          break;

        default:
          g_assert_not_reached ();
        }
    }

  va_end (ap);

  msg = soup_message_new (http_method, uri->str);

  g_string_free (uri, TRUE);

  return msg;
}

static void
vsx_connection_signal_error (VsxConnection *connection,
                             GError *error)
{
  g_signal_emit (connection,
                 signals[SIGNAL_GOT_ERROR],
                 0, /* detail */
                 error);
}

static void
vsx_connection_signal_error_from_message (VsxConnection *connection,
                                          SoupMessage *message)
{
  GError *error;

  error =
    g_error_new (SOUP_HTTP_ERROR,
                 message->status_code,
                 message->status_code == SOUP_STATUS_OK
                 ? "The HTTP connection finished"
                 : (message->status_code == 401
                    || message->status_code == 403)
                 ? "The HTTP authentication failed"
                 : SOUP_STATUS_IS_SERVER_ERROR (message->status_code)
                 ? "There was a server error"
                 : SOUP_STATUS_IS_CLIENT_ERROR (message->status_code)
                 ? "There was a client error"
                 : SOUP_STATUS_IS_TRANSPORT_ERROR (message->status_code)
                 ? "There was a transport error"
                 : "There was an error with the HTTP connection");

  vsx_connection_signal_error (connection, error);

  g_error_free (error);
}

static void
command_message_complete_cb (SoupSession *soup_session,
                             SoupMessage *message,
                             gpointer user_data)
{
  VsxConnection *connection = user_data;
  VsxConnectionPrivate *priv = connection->priv;

  priv->command_message = NULL;

  if (message->status_code != SOUP_STATUS_CANCELLED)
    {
      vsx_connection_maybe_send_command (connection);

      if (message->status_code != SOUP_STATUS_OK)
        vsx_connection_signal_error_from_message (connection, message);
    }
}

static void
vsx_connection_command_free (VsxConnectionCommand *cmd)
{
  g_free (cmd);
}

static void
vsx_connection_maybe_send_keep_alive (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  if (g_timer_elapsed (priv->keep_alive_time, NULL) >=
      VSX_CONNECTION_KEEP_ALIVE_TIME)
    {
      vsx_connection_queue_keep_alive (connection);

      priv->command_message
        = vsx_connection_make_message (connection,
                                       "GET",
                                       "keep_alive",
                                       "s",
                                       priv->person_id);

      soup_session_queue_message (priv->soup_session,
                                  priv->command_message,
                                  command_message_complete_cb,
                                  connection);
    }
}

static void
vsx_connection_maybe_send_command (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  /* If there's already a command in-progress then we'll wait until
     it's finished */
  if (priv->command_message)
    return;

  /* Wait until the conversation is in progress */
  if (priv->state != VSX_CONNECTION_STATE_IN_PROGRESS)
    return;

  if (g_queue_is_empty (&priv->command_queue))
    {
      if (priv->sent_typing_state != priv->typing)
        {
          vsx_connection_queue_keep_alive (connection);

          priv->sent_typing_state = priv->typing;

          priv->command_message
            = vsx_connection_make_message (connection,
                                           "GET",
                                           priv->typing
                                           ? "start_typing"
                                           : "stop_typing",
                                           "s",
                                           priv->person_id);

          soup_session_queue_message (priv->soup_session,
                                      priv->command_message,
                                      command_message_complete_cb,
                                      connection);
        }
      else
        vsx_connection_maybe_send_keep_alive (connection);
    }
  else
    {
      VsxConnectionCommand *cmd = vsx_connection_pop_command (connection);

      switch (cmd->type)
        {
        case VSX_CONNECTION_COMMAND_MESSAGE:
          {
            VsxConnectionMessageCommand *msg_cmd =
              (VsxConnectionMessageCommand *) cmd;

            priv->command_message
              = vsx_connection_make_message (connection,
                                             "POST",
                                             "send_message",
                                             "s",
                                             priv->person_id);

            soup_message_set_request (priv->command_message,
                                      "text/plain; charset=utf-8",
                                      SOUP_MEMORY_COPY,
                                      msg_cmd->text,
                                      strlen (msg_cmd->text));

            /* The server automatically assumes we're not typing anymore
               when the client sends a message */
            priv->sent_typing_state = FALSE;
          }
          break;

        case VSX_CONNECTION_COMMAND_LEAVE:
          priv->command_message
            = vsx_connection_make_message (connection,
                                           "GET",
                                           "leave",
                                           "s",
                                           priv->person_id);
          break;

        case VSX_CONNECTION_COMMAND_SHOUT:
          priv->command_message
            = vsx_connection_make_message (connection,
                                           "GET",
                                           "shout",
                                           "s",
                                           priv->person_id);
          break;

        case VSX_CONNECTION_COMMAND_TURN:
          priv->command_message
            = vsx_connection_make_message (connection,
                                           "GET",
                                           "turn",
                                           "s",
                                           priv->person_id);
          break;

        case VSX_CONNECTION_COMMAND_MOVE_TILE:
          {
            VsxConnectionMoveTileCommand *move_cmd =
              (VsxConnectionMoveTileCommand *) cmd;

            priv->command_message
              = vsx_connection_make_message (connection,
                                             "GET",
                                             "move_tile",
                                             "siii",
                                             priv->person_id,
                                             move_cmd->tile_num,
                                             move_cmd->x,
                                             move_cmd->y);
            break;
          }
        }

      vsx_connection_command_free (cmd);

      vsx_connection_queue_keep_alive (connection);

      soup_session_queue_message (priv->soup_session,
                                  priv->command_message,
                                  command_message_complete_cb,
                                  connection);
    }
}

static void
vsx_connection_add_command (VsxConnection *connection,
                            VsxConnectionCommand *cmd)
{
  VsxConnectionPrivate *priv = connection->priv;

  cmd->node.data = cmd;
  cmd->node.next = NULL;
  cmd->node.prev = NULL;

  g_queue_push_tail_link (&priv->command_queue, &cmd->node);

  vsx_connection_maybe_send_command (connection);
}

static void
vsx_connection_add_simple_command (VsxConnection *connection,
                                   VsxConnectionCommandType type)
{
  VsxConnectionPrivate *priv = connection->priv;
  VsxConnectionCommand *cmd;
  GList *l;

  /* Don't add the command if it's already in the queue */
  for (l = priv->command_queue.head; l; l = l->next)
    {
      cmd = l->data;

      if (cmd->type == type)
        return;
    }

  cmd = g_malloc (sizeof (VsxConnectionCommand));

  cmd->type = type;

  vsx_connection_add_command (connection, cmd);
}

static void
vsx_connection_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
  VsxConnection *connection = VSX_CONNECTION (object);

  switch (prop_id)
    {
    case PROP_RUNNING:
      g_value_set_boolean (value, vsx_connection_get_running (connection));
      break;

    case PROP_TYPING:
      g_value_set_boolean (value,
                           vsx_connection_get_typing (connection));
      break;

    case PROP_STATE:
      g_value_set_enum (value,
                        vsx_connection_get_state (connection));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
vsx_connection_set_property (GObject *object,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
  VsxConnection *connection = VSX_CONNECTION (object);
  VsxConnectionPrivate *priv = connection->priv;

  switch (prop_id)
    {
    case PROP_SERVER_BASE_URL:
      priv->server_base_url = g_strdup (g_value_get_string (value));
      break;

    case PROP_SOUP_SESSION:
      {
        SoupSession *soup_session = g_value_get_object (value);
        if (soup_session)
          priv->soup_session = g_object_ref (soup_session);
      }
      break;

    case PROP_ROOM:
      priv->room = g_strdup (g_value_get_string (value));
      break;

    case PROP_PLAYER_NAME:
      priv->player_name = g_strdup (g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static char *
find_terminator (char *buf, gsize len)
{
  char *end = buf + len;

  while (end - buf >= 2)
    {
      if (buf[0] == '\r' && buf[1] == '\n')
        return buf;
      buf++;
    }

  return NULL;
}

static gboolean
get_object_int_member (JsonObject *object,
                       const char *member,
                       gint64 *value)
{
  JsonNode *node = json_object_get_member (object, member);

  if (json_node_get_node_type (node) != JSON_NODE_VALUE)
    return FALSE;

  *value = json_node_get_int (node);

  return TRUE;
}

static gboolean
get_object_string_member (JsonObject *object,
                          const char *member,
                          const char **value)
{
  JsonNode *node = json_object_get_member (object, member);

  if (json_node_get_node_type (node) != JSON_NODE_VALUE)
    return FALSE;

  *value = json_node_get_string (node);

  return TRUE;
}

void
vsx_connection_set_typing (VsxConnection *connection,
                           gboolean typing)
{
  VsxConnectionPrivate *priv = connection->priv;

  if (priv->typing != typing)
    {
      priv->typing = typing;
      vsx_connection_maybe_send_command (connection);
      g_object_notify (G_OBJECT (connection), "typing");
    }
}

void
vsx_connection_shout (VsxConnection *connection)
{
  vsx_connection_add_simple_command (connection,
                                     VSX_CONNECTION_COMMAND_SHOUT);
}

void
vsx_connection_turn (VsxConnection *connection)
{
  vsx_connection_add_simple_command (connection,
                                     VSX_CONNECTION_COMMAND_TURN);
}

void
vsx_connection_move_tile (VsxConnection *connection,
                          int tile_num,
                          int x,
                          int y)
{
  VsxConnectionPrivate *priv = connection->priv;
  VsxConnectionMoveTileCommand *cmd;
  GList *l;

  /* If there is already a move command for this tile in the queue
   * then we'll just update the position instead */
  for (l = priv->command_queue.head; l; l = l->next)
    {
      cmd = l->data;

      if (cmd->parent.type == VSX_CONNECTION_COMMAND_MOVE_TILE &&
          cmd->tile_num == tile_num)
        {
          cmd->x = x;
          cmd->y = y;
          return;
        }
    }

  cmd = g_malloc (sizeof (VsxConnectionMoveTileCommand));

  cmd->parent.type = VSX_CONNECTION_COMMAND_MOVE_TILE;

  cmd->tile_num = tile_num;
  cmd->x = x;
  cmd->y = y;

  vsx_connection_add_command (connection, &cmd->parent);
}

static void
vsx_connection_set_state (VsxConnection *connection,
                          VsxConnectionState state)
{
  VsxConnectionPrivate *priv = connection->priv;

  if (priv->state != state)
    {
      priv->state = state;
      vsx_connection_maybe_send_command (connection);
      g_object_notify (G_OBJECT (connection), "state");
    }
}

static VsxPlayer *
get_or_create_player (VsxConnection *connection,
                      int player_num)
{
  VsxConnectionPrivate *priv = connection->priv;
  VsxPlayer *player =
    g_hash_table_lookup (priv->players, GINT_TO_POINTER (player_num));

  if (player == NULL)
    {
      player = g_slice_new0 (VsxPlayer);

      player->num = player_num;

      g_hash_table_insert (priv->players,
                           GINT_TO_POINTER (player_num),
                           player);
    }

  return player;
}

static gboolean
handle_player_name (VsxConnection *connection,
                    JsonArray *array)
{
  JsonNode *message_node;
  JsonObject *message_object;
  gint64 num;
  const char *text;
  VsxPlayer *player;

  if (json_array_get_length (array) < 2)
    return FALSE;

  message_node = json_array_get_element (array, 1);

  if (json_node_get_node_type (message_node) != JSON_NODE_OBJECT)
    return FALSE;

  message_object = json_node_get_object (message_node);

  if (!get_object_int_member (message_object, "num", &num) ||
      num < 0 || num > G_MAXINT ||
      !get_object_string_member (message_object, "name", &text))
    return FALSE;

  player = get_or_create_player (connection, num);

  g_free (player->name);
  player->name = g_strdup (text);

  g_signal_emit (connection,
                 signals[SIGNAL_PLAYER_CHANGED],
                 0, /* detail */
                 player);

  return TRUE;
}

static gboolean
handle_player (VsxConnection *connection,
               JsonArray *array)
{
  JsonNode *message_node;
  JsonObject *message_object;
  gint64 num, flags;
  VsxPlayer *player;

  if (json_array_get_length (array) < 2)
    return FALSE;

  message_node = json_array_get_element (array, 1);

  if (json_node_get_node_type (message_node) != JSON_NODE_OBJECT)
    return FALSE;

  message_object = json_node_get_object (message_node);

  if (!get_object_int_member (message_object, "num", &num) ||
      num < 0 || num > G_MAXINT ||
      !get_object_int_member (message_object, "flags", &flags) ||
      flags < 0 || flags > G_MAXINT)
    return FALSE;

  player = get_or_create_player (connection, num);

  player->flags = flags;

  g_signal_emit (connection,
                 signals[SIGNAL_PLAYER_CHANGED],
                 0, /* detail */
                 player);

  return TRUE;
}

static gboolean
handle_tile (VsxConnection *connection,
             JsonArray *array)
{
  VsxConnectionPrivate *priv = connection->priv;
  JsonNode *message_node;
  JsonObject *message_object;
  gint64 num, x, y;
  const char *letter;
  VsxTile *tile;
  gboolean is_new = FALSE;

  if (json_array_get_length (array) < 2)
    return FALSE;

  message_node = json_array_get_element (array, 1);

  if (json_node_get_node_type (message_node) != JSON_NODE_OBJECT)
    return FALSE;

  message_object = json_node_get_object (message_node);

  if (!get_object_int_member (message_object, "num", &num) ||
      num < 0 || num > G_MAXINT ||
      !get_object_int_member (message_object, "x", &x) ||
      x < G_MININT16 || x > G_MAXINT16 ||
      !get_object_int_member (message_object, "y", &y) ||
      y < G_MININT16 || y > G_MAXINT16 ||
      !get_object_string_member (message_object, "letter", &letter) ||
      g_utf8_strlen (letter, -1) != 1)
    return FALSE;

  tile = g_hash_table_lookup (priv->tiles, GINT_TO_POINTER ((int) num));

  if (tile == NULL)
    {
      tile = g_slice_new0 (VsxTile);
      tile->num = num;

      g_hash_table_insert (priv->tiles, GINT_TO_POINTER ((int) num), tile);
      is_new = TRUE;
    }

  tile->x = x;
  tile->y = y;
  tile->letter = g_utf8_get_char (letter);

  g_signal_emit (connection,
                 signals[SIGNAL_TILE_CHANGED],
                 0, /* detail */
                 is_new,
                 tile);

  return TRUE;
}

static gboolean
handle_shout (VsxConnection *connection,
              JsonArray *array)
{
  JsonNode *num_node;
  gint64 num;
  VsxPlayer *player;

  if (json_array_get_length (array) < 2)
    return FALSE;

  num_node = json_array_get_element (array, 1);

  if (json_node_get_node_type (num_node) != JSON_NODE_VALUE)
    return FALSE;

  num = json_node_get_int (num_node);

  if (num < 0 || num > G_MAXINT)
    return FALSE;

  player = get_or_create_player (connection, num);

  g_signal_emit (connection,
                 signals[SIGNAL_PLAYER_SHOUTED],
                 0, /* detail */
                 player);

  return TRUE;
}

static gboolean
handle_message (VsxConnection *connection,
                JsonNode *object,
                GError **error)
{
  VsxConnectionPrivate *priv = connection->priv;
  JsonArray *array;
  JsonNode *method_node;
  const char *method_string;

  if (json_node_get_node_type (object) != JSON_NODE_ARRAY
      || (array = json_node_get_array (object)) == NULL
      || json_array_get_length (array) < 1
      || (json_node_get_node_type (method_node
                                   = json_array_get_element (array, 0))
          != JSON_NODE_VALUE))
    goto bad_data;

  method_string = json_node_get_string (method_node);

  if (!strcmp (method_string, "header"))
    {
      JsonNode *header_node;
      JsonObject *header_object;
      const char *person_id;
      gint64 self_num;

      if (json_array_get_length (array) < 2)
        goto bad_data;

      header_node = json_array_get_element (array, 1);

      if (json_node_get_node_type (header_node) != JSON_NODE_OBJECT)
        goto bad_data;

      header_object = json_node_get_object (header_node);

      if (!get_object_int_member (header_object, "num", &self_num)
          || !get_object_string_member (header_object, "id", &person_id))
        goto bad_data;

      priv->self = get_or_create_player (connection, self_num);

      g_free (priv->person_id);
      priv->person_id = g_strdup (person_id);

      if (priv->state == VSX_CONNECTION_STATE_AWAITING_HEADER)
        vsx_connection_set_state (connection, VSX_CONNECTION_STATE_IN_PROGRESS);
    }
  else if (!strcmp (method_string, "message"))
    {
      JsonNode *message_node;
      JsonObject *message_object;
      gint64 num;
      const char *text;

      if (json_array_get_length (array) < 2)
        goto bad_data;

      message_node = json_array_get_element (array, 1);

      if (json_node_get_node_type (message_node) != JSON_NODE_OBJECT)
        goto bad_data;

      message_object = json_node_get_object (message_node);

      if (!get_object_int_member (message_object, "person", &num) ||
          num < 0 || num > G_MAXINT ||
          !get_object_string_member (message_object, "text", &text))
        goto bad_data;

      g_signal_emit (connection,
                     signals[SIGNAL_MESSAGE],
                     0, /* detail */
                     get_or_create_player (connection, num),
                     text);

      priv->next_message_num++;
    }
  else if (!strcmp (method_string, "tile"))
    {
      if (!handle_tile (connection, array))
        goto bad_data;
    }
  else if (!strcmp (method_string, "shout"))
    {
      if (!handle_shout (connection, array))
        goto bad_data;
    }
  else if (!strcmp (method_string, "end"))
    vsx_connection_set_state (connection, VSX_CONNECTION_STATE_DONE);
  else if (!strcmp (method_string, "player-name"))
    {
      if (!handle_player_name (connection, array))
        goto bad_data;
    }
  else if (!strcmp (method_string, "player"))
    {
      if (!handle_player (connection, array))
        goto bad_data;
    }

  return TRUE;

 bad_data:
  g_set_error (error,
               VSX_CONNECTION_ERROR,
               VSX_CONNECTION_ERROR_BAD_DATA,
               "Bad data received from the server");
  return FALSE;
}

static void
vsx_connection_process_lines (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;
  char *buf_pos, *end;
  gsize len;

  g_object_ref (connection);

  /* Mark that we're still connected so that we can detect if one of
     the signal emissions ends up disconnecting the stream. In that
     case we'd want to stop further processing */
  priv->has_disconnected = FALSE;

  if (priv->json_parser == NULL)
    priv->json_parser = json_parser_new ();

  buf_pos = priv->line_buffer->str;
  len = priv->line_buffer->len;

  while (!priv->has_disconnected
         && (end = find_terminator (buf_pos, len)))
    {
      GError *error = NULL;

      if (end > buf_pos)
        {
          if (!json_parser_load_from_data (priv->json_parser,
                                           buf_pos,
                                           end - buf_pos,
                                           &error)
              || !handle_message (connection,
                                  json_parser_get_root (priv->json_parser),
                                  &error))

            {
              /* If the stream is giving us invalid JSON data then we'll
                 just reconnect as if it was an error */

              soup_session_cancel_message (priv->soup_session,
                                           priv->message,
                                           SOUP_STATUS_CANCELLED);

              vsx_connection_signal_error (connection, error);

              g_clear_error (&error);

              break;
            }
        }

      len -= end - buf_pos + 2;
      buf_pos = end + 2;
    }

  /* Move the unprocessed data to the beginning of the buffer in case
     the chunk contained an incomplete line */
  if (buf_pos != priv->line_buffer->str)
    {
      memmove (priv->line_buffer->str, buf_pos, len);
      g_string_set_size (priv->line_buffer, len);
    }

  g_object_unref (connection);
}

static void
vsx_connection_got_chunk_cb (SoupMessage *message,
                             SoupBuffer *chunk,
                             VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  /* We are processing the data as it comes in so we don't need to
     preserve the old contents of the stream. We have to do this here
     because libsoup doesn't guarantee that the message_body will
     exist until data is first sent */
  soup_message_body_set_accumulate (message->response_body, FALSE);

  /* Ignore the message body if we didn't get a successful
     connection */
  if (message->status_code == SOUP_STATUS_OK)
    {
      g_string_append_len (priv->line_buffer, chunk->data, chunk->length);

      /* This may cause the message to be cancelled if the data is
         invalid or if the signal emission disconnects the stream */
      vsx_connection_process_lines (connection);
    }
}

static void
vsx_connection_got_headers_cb (SoupMessage *message,
                               VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  /* Every time we get a successful connection we'll reset the
     reconnect timeout */
  if (message->status_code == SOUP_STATUS_OK)
    {
      priv->reconnect_timeout = VSX_CONNECTION_INITIAL_TIMEOUT;

      g_string_set_size (priv->line_buffer, 0);
    }
}

static gboolean
vsx_connection_reconnect_cb (gpointer user_data)
{
  VsxConnection *connection = user_data;

  /* Queue a reconnect. This will switch back to the running state */
  vsx_connection_queue_message (connection);

  /* Remove the handler */
  return FALSE;
}

static void
vsx_connection_queue_reconnect (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  priv->reconnect_handler =
    g_timeout_add_seconds (priv->reconnect_timeout,
                           vsx_connection_reconnect_cb,
                           connection);
  /* Next time we need to try to reconnect we'll delay for twice
     as long, up to the maximum timeout */
  priv->reconnect_timeout *= 2;
  if (priv->reconnect_timeout > VSX_CONNECTION_MAX_TIMEOUT)
    priv->reconnect_timeout = VSX_CONNECTION_MAX_TIMEOUT;

  priv->running_state = VSX_CONNECTION_RUNNING_STATE_WAITING_FOR_RECONNECT;
}

static void
vsx_connection_message_completed_cb (SoupSession *soup_session,
                                     SoupMessage *message,
                                     gpointer user_data)
{
  VsxConnection *connection = user_data;
  VsxConnectionPrivate *priv = connection->priv;

  if (priv->keep_alive_timeout)
    {
      g_source_remove (priv->keep_alive_timeout);
      priv->keep_alive_timeout = 0;
    }

  /* If the message was cancelled then we'll assume these came from a
     request to stop running so we'll switch to the disconnected
     state */
  if (message->status_code == SOUP_STATUS_CANCELLED)
    priv->running_state = VSX_CONNECTION_RUNNING_STATE_DISCONNECTED;
  /* If the message is complete and the conversation is over then
     there's no point in connecting again because we'll just get a
     copy of the conversation again */
  else if (message->status_code == SOUP_STATUS_OK
           && priv->state == VSX_CONNECTION_STATE_DONE)
    {
      priv->running_state = VSX_CONNECTION_RUNNING_STATE_DISCONNECTED;
      g_object_notify (G_OBJECT (connection), "running");
    }
  /* If the connection just ended without an error then we'll try to
     reconnect immediately */
  else
    {
      vsx_connection_queue_reconnect (connection);

      vsx_connection_signal_error_from_message (connection, message);
    }
}

static void
vsx_connection_queue_message (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  if (priv->person_id)
    priv->message = vsx_connection_make_message (connection,
                                                 "GET",
                                                 "watch_person",
                                                 "si",
                                                 priv->person_id,
                                                 priv->next_message_num);
  else
    priv->message = vsx_connection_make_message (connection,
                                                 "GET",
                                                 "new_person",
                                                 "ss",
                                                 priv->room,
                                                 priv->player_name);


  g_signal_connect (priv->message, "got-headers",
                    G_CALLBACK (vsx_connection_got_headers_cb),
                    connection);

  g_signal_connect (priv->message, "got-chunk",
                    G_CALLBACK (vsx_connection_got_chunk_cb),
                    connection);

  vsx_connection_queue_keep_alive (connection);

  soup_session_queue_message (priv->soup_session,
                              priv->message,
                              vsx_connection_message_completed_cb,
                              connection);

  priv->running_state = VSX_CONNECTION_RUNNING_STATE_RUNNING;
}

static void
vsx_connection_constructed (GObject *object)
{
  VsxConnection *connection = VSX_CONNECTION (object);
  VsxConnectionPrivate *priv = connection->priv;

  if (priv->soup_session == NULL)
    priv->soup_session = soup_session_async_new ();
}

static void
vsx_connection_set_running_internal (VsxConnection *connection,
                                     gboolean running)
{
  VsxConnectionPrivate *priv = connection->priv;

  if (running)
    {
      if (priv->running_state == VSX_CONNECTION_RUNNING_STATE_DISCONNECTED)
        {
          /* Reset the retry timeout because this is a first attempt
             at connecting */
          priv->reconnect_timeout = VSX_CONNECTION_INITIAL_TIMEOUT;
          vsx_connection_queue_message (connection);
        }
    }
  else
    {
      /* Mark that we've disconnected so that if we're in the middle
         of processing lines we'll bail out */
      priv->has_disconnected = TRUE;

      switch (priv->running_state)
        {
        case VSX_CONNECTION_RUNNING_STATE_DISCONNECTED:
          /* already disconnected */
          break;

        case VSX_CONNECTION_RUNNING_STATE_RUNNING:
          /* This will also set the disconnected state */
          soup_session_cancel_message (priv->soup_session,
                                       priv->message,
                                       SOUP_STATUS_CANCELLED);
          break;

        case VSX_CONNECTION_RUNNING_STATE_WAITING_FOR_RECONNECT:
          /* Cancel the timeout */
          g_source_remove (priv->reconnect_handler);
          priv->running_state = VSX_CONNECTION_RUNNING_STATE_DISCONNECTED;
          break;
        }
    }
}

void
vsx_connection_set_running (VsxConnection *connection,
                            gboolean running)
{
  g_return_if_fail (VSX_IS_CONNECTION (connection));

  vsx_connection_set_running_internal (connection, running);

  g_object_notify (G_OBJECT (connection), "running");
}

gboolean
vsx_connection_get_running (VsxConnection *connection)
{
  g_return_val_if_fail (VSX_IS_CONNECTION (connection), FALSE);

  return (connection->priv->running_state
          != VSX_CONNECTION_RUNNING_STATE_DISCONNECTED);
}

static void
vsx_connection_class_init (VsxConnectionClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GParamSpec *pspec;

  gobject_class->dispose = vsx_connection_dispose;
  gobject_class->finalize = vsx_connection_finalize;
  gobject_class->constructed = vsx_connection_constructed;
  gobject_class->set_property = vsx_connection_set_property;
  gobject_class->get_property = vsx_connection_get_property;

  pspec = g_param_spec_string ("server-base-url",
                               "Server base URL",
                               "The base URL of the server to connect to",
                               "http://vs.busydoingnothing.co.uk:5142/",
                               G_PARAM_WRITABLE
                               | G_PARAM_CONSTRUCT_ONLY
                               | G_PARAM_STATIC_NAME
                               | G_PARAM_STATIC_NICK
                               | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (gobject_class, PROP_SERVER_BASE_URL, pspec);

  pspec = g_param_spec_string ("room",
                               "Room to connect to",
                               "The name of the room to connect to",
                               "english",
                               G_PARAM_WRITABLE
                               | G_PARAM_CONSTRUCT_ONLY
                               | G_PARAM_STATIC_NAME
                               | G_PARAM_STATIC_NICK
                               | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (gobject_class, PROP_ROOM, pspec);

  pspec = g_param_spec_string ("player-name",
                               "Player name",
                               "Name of the player",
                               "player",
                               G_PARAM_WRITABLE
                               | G_PARAM_CONSTRUCT_ONLY
                               | G_PARAM_STATIC_NAME
                               | G_PARAM_STATIC_NICK
                               | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (gobject_class, PROP_PLAYER_NAME, pspec);

  pspec = g_param_spec_object ("soup-session",
                               "Soup session",
                               "A soup session to use",
                               SOUP_TYPE_SESSION,
                               G_PARAM_WRITABLE
                               | G_PARAM_CONSTRUCT_ONLY
                               | G_PARAM_STATIC_NAME
                               | G_PARAM_STATIC_NICK
                               | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (gobject_class, PROP_SOUP_SESSION, pspec);

  pspec = g_param_spec_boolean ("running",
                                "Running",
                                "Whether the stream connection should be "
                                "trying to connect and receive objects",
                                FALSE,
                                G_PARAM_READABLE
                                | G_PARAM_WRITABLE
                                | G_PARAM_STATIC_NAME
                                | G_PARAM_STATIC_NICK
                                | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (gobject_class, PROP_RUNNING, pspec);

  pspec = g_param_spec_boolean ("typing",
                                "Typing",
                                "Whether the user is typing.",
                                FALSE,
                                G_PARAM_READABLE
                                | G_PARAM_STATIC_NAME
                                | G_PARAM_STATIC_NICK
                                | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (gobject_class, PROP_TYPING, pspec);

  pspec = g_param_spec_enum ("state",
                             "State",
                             "State of the conversation",
                             VSX_TYPE_CONNECTION_STATE,
                             VSX_CONNECTION_STATE_AWAITING_HEADER,
                             G_PARAM_READABLE
                             | G_PARAM_STATIC_NAME
                             | G_PARAM_STATIC_NICK
                             | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (gobject_class, PROP_STATE, pspec);

  signals[SIGNAL_GOT_ERROR] =
    g_signal_new ("got-error",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (VsxConnectionClass, got_error),
                  NULL, /* accumulator */
                  NULL, /* accumulator data */
                  vsx_marshal_VOID__BOXED,
                  G_TYPE_NONE,
                  1, /* num arguments */
                  G_TYPE_ERROR);

  signals[SIGNAL_MESSAGE] =
    g_signal_new ("message",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (VsxConnectionClass, message),
                  NULL, /* accumulator */
                  NULL, /* accumulator data */
                  vsx_marshal_VOID__POINTER_STRING,
                  G_TYPE_NONE,
                  2, /* num arguments */
                  G_TYPE_POINTER,
                  G_TYPE_STRING);

  signals[SIGNAL_PLAYER_CHANGED] =
    g_signal_new ("player-changed",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (VsxConnectionClass, player_changed),
                  NULL, /* accumulator */
                  NULL, /* accumulator data */
                  vsx_marshal_VOID__POINTER,
                  G_TYPE_NONE,
                  1, /* num arguments */
                  G_TYPE_POINTER);

  signals[SIGNAL_TILE_CHANGED] =
    g_signal_new ("tile-changed",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (VsxConnectionClass, tile_changed),
                  NULL, /* accumulator */
                  NULL, /* accumulator data */
                  vsx_marshal_VOID__BOOL_POINTER,
                  G_TYPE_NONE,
                  2, /* num arguments */
                  G_TYPE_BOOLEAN,
                  G_TYPE_POINTER);

  signals[SIGNAL_PLAYER_SHOUTED] =
    g_signal_new ("player-shouted",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (VsxConnectionClass, player_shouted),
                  NULL, /* accumulator */
                  NULL, /* accumulator data */
                  vsx_marshal_VOID__POINTER,
                  G_TYPE_NONE,
                  1, /* num arguments */
                  G_TYPE_POINTER);

  g_type_class_add_private (klass, sizeof (VsxConnectionPrivate));
}

static void
free_player_cb (void *data)
{
  VsxPlayer *player = data;

  g_free (player->name);
  g_slice_free (VsxPlayer, player);
}

static void
free_tile_cb (void *data)
{
  VsxTile *tile = data;

  g_slice_free (VsxTile, tile);
}

static void
vsx_connection_init (VsxConnection *self)
{
  VsxConnectionPrivate *priv;

  priv = self->priv = VSX_CONNECTION_GET_PRIVATE (self);

  priv->line_buffer = g_string_new (NULL);
  priv->next_message_num = 0;
  g_queue_init (&priv->command_queue);

  priv->keep_alive_time = g_timer_new ();

  priv->players = g_hash_table_new_full (g_direct_hash,
                                         g_direct_equal,
                                         NULL, /* key_destroy */
                                         free_player_cb);
  priv->tiles = g_hash_table_new_full (g_direct_hash,
                                       g_direct_equal,
                                       NULL, /* key_destroy */
                                       free_tile_cb);
}

static void
vsx_connection_dispose (GObject *object)
{
  VsxConnection *self = (VsxConnection *) object;
  VsxConnectionPrivate *priv = self->priv;

  vsx_connection_set_running_internal (self, FALSE);

  if (priv->command_message)
    soup_session_cancel_message (priv->soup_session,
                                 priv->command_message,
                                 SOUP_STATUS_CANCELLED);

  if (priv->soup_session)
    {
      g_object_unref (priv->soup_session);
      priv->soup_session = NULL;
    }

  if (priv->json_parser)
    {
      g_object_unref (priv->json_parser);
      priv->json_parser = NULL;
    }

  G_OBJECT_CLASS (vsx_connection_parent_class)->dispose (object);
}

static void
vsx_connection_finalize (GObject *object)
{
  VsxConnection *self = (VsxConnection *) object;
  VsxConnectionPrivate *priv = self->priv;

  while (!g_queue_is_empty (&priv->command_queue))
    {
      VsxConnectionCommand *cmd = vsx_connection_pop_command (self);
      vsx_connection_command_free (cmd);
    }

  g_free (priv->server_base_url);
  g_free (priv->room);
  g_free (priv->player_name);
  g_free (priv->person_id);

  g_string_free (priv->line_buffer, TRUE);

  g_timer_destroy (priv->keep_alive_time);

  g_hash_table_destroy (priv->players);
  g_hash_table_destroy (priv->tiles);

  G_OBJECT_CLASS (vsx_connection_parent_class)->finalize (object);
}

VsxConnection *
vsx_connection_new (SoupSession *soup_session,
                    const char *server_base_url,
                    const char *room,
                    const char *player_name)
{
  VsxConnection *self = g_object_new (VSX_TYPE_CONNECTION,
                                      "soup-session", soup_session,
                                      "server-base-url", server_base_url,
                                      "room", room,
                                      "player-name", player_name,
                                      NULL);

  return self;
}

gboolean
vsx_connection_get_typing (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  return priv->typing;
}

VsxConnectionState
vsx_connection_get_state (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  return priv->state;
}

void
vsx_connection_send_message (VsxConnection *connection,
                             const char *message)
{
  VsxConnectionMessageCommand *cmd;
  int text_len = strlen (message);

  cmd = g_malloc (sizeof (VsxConnectionMessageCommand) + text_len);

  cmd->parent.type = VSX_CONNECTION_COMMAND_MESSAGE;

  memcpy (cmd->text, message, text_len);
  cmd->text[text_len] = '\0';

  vsx_connection_add_command (connection, &cmd->parent);
}

void
vsx_connection_leave (VsxConnection *connection)
{
  vsx_connection_add_simple_command (connection,
                                     VSX_CONNECTION_COMMAND_LEAVE);
}

const VsxPlayer *
vsx_connection_get_player (VsxConnection *connection,
                           int player_num)
{
  VsxConnectionPrivate *priv = connection->priv;

  return g_hash_table_lookup (priv->players,
                              GINT_TO_POINTER (player_num));
}

typedef struct
{
  VsxConnectionForeachPlayerCallback callback;
  void *user_data;
} ForeachPlayerData;

static void
foreach_player_cb (void *key,
                   void *value,
                   void *user_data)
{
  ForeachPlayerData *data = user_data;

  data->callback (value, data->user_data);
}

void
vsx_connection_foreach_player (VsxConnection *connection,
                               VsxConnectionForeachPlayerCallback callback,
                               void *user_data)
{
  VsxConnectionPrivate *priv = connection->priv;
  ForeachPlayerData data;

  data.callback = callback;
  data.user_data = user_data;

  g_hash_table_foreach (priv->players, foreach_player_cb, &data);
}

const VsxPlayer *
vsx_connection_get_self (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  return priv->self;
}

const VsxTile *
vsx_connection_get_tile (VsxConnection *connection,
                         int tile_num)
{
  VsxConnectionPrivate *priv = connection->priv;

  return g_hash_table_lookup (priv->tiles,
                              GINT_TO_POINTER (tile_num));
}

typedef struct
{
  VsxConnectionForeachTileCallback callback;
  void *user_data;
} ForeachTileData;

static void
foreach_tile_cb (void *key,
                 void *value,
                 void *user_data)
{
  ForeachTileData *data = user_data;

  data->callback (value, data->user_data);
}

void
vsx_connection_foreach_tile (VsxConnection *connection,
                             VsxConnectionForeachTileCallback callback,
                             void *user_data)
{
  VsxConnectionPrivate *priv = connection->priv;
  ForeachTileData data;

  data.callback = callback;
  data.user_data = user_data;

  g_hash_table_foreach (priv->tiles, foreach_tile_cb, &data);
}

GQuark
vsx_connection_error_quark (void)
{
  return g_quark_from_static_string ("vsx-connection-error-quark");
}
