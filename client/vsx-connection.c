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

#include "vsx-connection.h"

#include <string.h>
#include <stdarg.h>

#include "vsx-marshal.h"
#include "vsx-enum-types.h"
#include "vsx-player-private.h"
#include "vsx-tile-private.h"

enum
{
  PROP_0,

  PROP_SERVER_BASE_URL,
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
  guint reconnect_timeout;
  guint reconnect_handler;
  VsxPlayer *self;
  guint64 person_id;
  VsxConnectionRunningState running_state;
  VsxConnectionState state;
  gboolean typing;
  gboolean sent_typing_state;
  int next_message_num;

  GHashTable *players;
  GHashTable *tiles;

  /* A timeout for sending a keep alive message */
  guint keep_alive_timeout;
  GTimer *keep_alive_time;
};

static gboolean
vsx_connection_keep_alive_cb (void *data)
{
  VsxConnection *connection = data;
  VsxConnectionPrivate *priv = connection->priv;

  priv->keep_alive_timeout = 0;

  /* FIXME */

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

void
vsx_connection_set_typing (VsxConnection *connection,
                           gboolean typing)
{
  VsxConnectionPrivate *priv = connection->priv;

  if (priv->typing != typing)
    {
      priv->typing = typing;
      /* FIXME */
      g_object_notify (G_OBJECT (connection), "typing");
    }
}

void
vsx_connection_shout (VsxConnection *connection)
{
  /* FIXME */
}

void
vsx_connection_turn (VsxConnection *connection)
{
  /* FIXME */
}

void
vsx_connection_move_tile (VsxConnection *connection,
                          int tile_num,
                          int x,
                          int y)
{
  /* FIXME */
}

static void
vsx_connection_set_state (VsxConnection *connection,
                          VsxConnectionState state)
{
  VsxConnectionPrivate *priv = connection->priv;

  if (priv->state != state)
    {
      priv->state = state;
      /* FIXME */
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
vsx_connection_reconnect_cb (gpointer user_data)
{
  VsxConnection *connection = user_data;

  /* FIXME */

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
          /* FIXME */
        }
    }
  else
    {
      switch (priv->running_state)
        {
        case VSX_CONNECTION_RUNNING_STATE_DISCONNECTED:
          /* already disconnected */
          break;

        case VSX_CONNECTION_RUNNING_STATE_RUNNING:
          /* FIXME */
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
                  vsx_marshal_VOID__BOOLEAN_POINTER,
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

  priv->next_message_num = 0;

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

  G_OBJECT_CLASS (vsx_connection_parent_class)->dispose (object);
}

static void
vsx_connection_finalize (GObject *object)
{
  VsxConnection *self = (VsxConnection *) object;
  VsxConnectionPrivate *priv = self->priv;

  g_free (priv->server_base_url);
  g_free (priv->room);
  g_free (priv->player_name);

  g_timer_destroy (priv->keep_alive_time);

  g_hash_table_destroy (priv->players);
  g_hash_table_destroy (priv->tiles);

  G_OBJECT_CLASS (vsx_connection_parent_class)->finalize (object);
}

VsxConnection *
vsx_connection_new (const char *server_base_url,
                    const char *room,
                    const char *player_name)
{
  VsxConnection *self = g_object_new (VSX_TYPE_CONNECTION,
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
  /* FIXME */
}

void
vsx_connection_leave (VsxConnection *connection)
{
  /* FIXME */
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
