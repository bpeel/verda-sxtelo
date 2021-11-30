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
#include <gio/gio.h>

#include "vsx-marshal.h"
#include "vsx-enum-types.h"
#include "vsx-player-private.h"
#include "vsx-tile-private.h"
#include "vsx-proto.h"

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

static const guint8
ws_terminator[] = "\r\n\r\n";

#define WS_TERMINATOR_LENGTH ((sizeof ws_terminator) - 1)

typedef enum
{
  VSX_CONNECTION_DIRTY_FLAG_WS_HEADER = (1 << 0),
  VSX_CONNECTION_DIRTY_FLAG_HEADER = (1 << 1),
  VSX_CONNECTION_DIRTY_FLAG_KEEP_ALIVE = (1 << 2),
} VsxConnectionDirtyFlag;

typedef int (* VsxConnectionWriteStateFunc) (VsxConnection *conn,
                                             guint8 *buffer,
                                             size_t buffer_size);

static void
vsx_connection_dispose (GObject *object);

static void
vsx_connection_finalize (GObject *object);

static void
vsx_connection_queue_reconnect (VsxConnection *connection);

static void
update_poll (VsxConnection *connection);

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
  /* g_socket_connect has been called and we are waiting for it to
   * become ready for writing */
  VSX_CONNECTION_RUNNING_STATE_RECONNECTING,
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
  gboolean has_person_id;
  guint64 person_id;
  VsxConnectionRunningState running_state;
  VsxConnectionState state;
  gboolean typing;
  gboolean sent_typing_state;
  int next_message_num;

  VsxConnectionDirtyFlag dirty_flags;

  GSocket *sock;
  GIOChannel *sock_channel;
  guint sock_source;
  /* The condition that the source was last created with so we can
   * know if we need to recreate it.
   */
  GIOCondition sock_condition;

  GHashTable *players;
  GHashTable *tiles;

  /* A timeout for sending a keep alive message */
  guint keep_alive_timeout;
  GTimer *keep_alive_time;

  unsigned int output_length;
  guint8 output_buffer[VSX_PROTO_MAX_PAYLOAD_SIZE
                       + VSX_PROTO_MAX_FRAME_HEADER_LENGTH];

  unsigned int input_length;
  guint8 input_buffer[VSX_PROTO_MAX_PAYLOAD_SIZE
                      + VSX_PROTO_MAX_FRAME_HEADER_LENGTH];

  /* Position within ws terminator that we have found so far. If this
   * is greater than the terminator length then we’ve finished the
   * WebSocket negotation.
   */
  unsigned int ws_terminator_pos;
};

static gboolean
vsx_connection_keep_alive_cb (void *data)
{
  VsxConnection *connection = data;
  VsxConnectionPrivate *priv = connection->priv;

  priv->keep_alive_timeout = 0;

  priv->dirty_flags |= VSX_CONNECTION_DIRTY_FLAG_KEEP_ALIVE;

  update_poll (connection);

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
close_socket (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  if (priv->keep_alive_timeout)
    {
      g_source_remove (priv->keep_alive_timeout);
      priv->keep_alive_timeout = 0;
    }

  if (priv->sock_source)
    {
      g_source_remove (priv->sock_source);
      priv->sock_source = 0;
      priv->sock_condition = 0;
    }

  if (priv->sock_channel)
    {
      g_io_channel_unref (priv->sock_channel);
      priv->sock_channel = NULL;
    }

  if (priv->sock)
    {
      g_socket_close (priv->sock, NULL);
      g_object_unref (priv->sock);
      priv->sock = NULL;
    }
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
handle_message (VsxConnection *connection,
                const guint8 *payload,
                size_t payload_length,
                GError **error)
{
  VsxConnectionPrivate *priv = connection->priv;
  guint8 person;
  const char *text;

  if (!vsx_proto_read_payload (payload + 1,
                               payload_length - 1,

                               VSX_PROTO_TYPE_UINT8,
                               &person,

                               VSX_PROTO_TYPE_STRING,
                               &text,

                               VSX_PROTO_TYPE_NONE))
    {
      g_set_error (error,
                   VSX_CONNECTION_ERROR,
                   VSX_CONNECTION_ERROR_BAD_DATA,
                   "The server sent an invalid message command");
      return FALSE;
    }

  priv->next_message_num++;

  g_signal_emit (connection,
                 signals[SIGNAL_MESSAGE],
                 0, /* detail */
                 get_or_create_player (connection, person),
                 text);

  return TRUE;
}

static gboolean
process_message (VsxConnection *connection,
                 const guint8 *payload,
                 size_t payload_length,
                 GError **error)
{
  if (payload_length < 1)
    {
      g_set_error (error,
                   VSX_CONNECTION_ERROR,
                   VSX_CONNECTION_ERROR_BAD_DATA,
                   "The server sent an empty message");
      return FALSE;
    }

  switch (payload[0])
    {
    case VSX_PROTO_MESSAGE:
      return handle_message (connection, payload, payload_length, error);
    }

  return TRUE;
}

static void
report_error (VsxConnection *connection,
              GError *error)
{
  close_socket (connection);
  vsx_connection_queue_reconnect (connection);
  vsx_connection_signal_error (connection, error);
}

static gboolean
get_payload_length (const guint8 *buf,
                    size_t buf_length,
                    size_t *payload_length_out,
                    const guint8 **payload_start_out)
{
  if (buf_length < 2)
    return FALSE;

  switch (buf[1])
    {
    case 0x7e:
      if (buf_length < 4)
        return FALSE;

      *payload_length_out = vsx_proto_read_guint16 (buf + 2);
      *payload_start_out = buf + 4;
      return TRUE;

    case 0x7f:
      if (buf_length < 6)
        return FALSE;

      *payload_length_out = vsx_proto_read_guint32 (buf + 2);
      *payload_start_out = buf + 6;
      return TRUE;

    default:
      *payload_length_out = buf[1];
      *payload_start_out = buf + 2;
      return TRUE;
    }
}

static void
handle_read (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;
  GError *error = NULL;

  gssize got = g_socket_receive (priv->sock,
                                 (char *) priv->input_buffer
                                 + priv->input_length,
                                 (sizeof priv->input_buffer)
                                 - priv->input_length,
                                 NULL, /* cancellable */
                                 &error);
  if (got == -1)
    {
      if (error->domain != G_IO_ERROR
          || error->code != G_IO_ERROR_WOULD_BLOCK)
          report_error (connection, error);

      g_clear_error (&error);
    }
  else if (got == 0)
    {
      error = g_error_new (VSX_CONNECTION_ERROR,
                           VSX_CONNECTION_ERROR_CONNECTION_CLOSED,
                           "The server unexpectedly closed the connection");
      report_error (connection, error);
      g_error_free (error);
    }
  else
    {
      const guint8 *p = priv->input_buffer;

      priv->input_length += got;

      if (priv->ws_terminator_pos < WS_TERMINATOR_LENGTH)
        {
          while (p - priv->input_buffer < priv->input_length)
            {
              if (*(p++) == ws_terminator[priv->ws_terminator_pos])
                {
                  if (++priv->ws_terminator_pos >= WS_TERMINATOR_LENGTH)
                    goto terminated;
                }
              else
                {
                  priv->ws_terminator_pos = 0;
                }
            }

          /* If we make it here then we haven’t found the end of the
           * terminator yet.
           */
          priv->input_length = 0;
          return;
        }

    terminated: ((void) 0);

      size_t payload_length;
      const guint8 *payload_start;

      while (get_payload_length (p,
                                 priv->input_buffer
                                 + priv->input_length
                                 - p,
                                 &payload_length,
                                 &payload_start))
        {
          if (payload_length > VSX_PROTO_MAX_PAYLOAD_SIZE)
            {
              error = g_error_new (VSX_CONNECTION_ERROR,
                                   VSX_CONNECTION_ERROR_CONNECTION_CLOSED,
                                   "The server sent a frame that is too long");
              report_error (connection, error);
              g_error_free (error);
              return;
            }

          if (payload_start + payload_length
              > priv->input_buffer + priv->input_length)
            break;

          /* Ignore control frames and non-binary frames */
          if (*p == 0x82
              && !process_message (connection,
                                   payload_start, payload_length,
                                   &error))
            {
              report_error (connection, error);
              g_error_free (error);
              return;
            }

          p = payload_start + payload_length;
        }

      memmove (priv->input_buffer,
               p,
               priv->input_buffer + priv->input_length - p);
      priv->input_length -= p - priv->input_buffer;
    }
}

static int
write_ws_request (VsxConnection *connection,
                  guint8 *buffer,
                  size_t buffer_size)
{
  static const guint8 ws_request[] =
    "GET / HTTP/1.1\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
    "\r\n";
  const size_t ws_request_len = (sizeof ws_request) - 1;

  if (buffer_size < ws_request_len)
    return -1;

  memcpy (buffer, ws_request, ws_request_len);

  return ws_request_len;
}

static int
write_header (VsxConnection *connection,
              guint8 *buffer,
              size_t buffer_size)
{
  VsxConnectionPrivate *priv = connection->priv;

  if (priv->has_person_id)
    {
      return vsx_proto_write_command (buffer,
                                      buffer_size,

                                      VSX_PROTO_RECONNECT,

                                      VSX_PROTO_TYPE_UINT64,
                                      priv->person_id,

                                      VSX_PROTO_TYPE_UINT16,
                                      priv->next_message_num,

                                      VSX_PROTO_TYPE_NONE);
    }
  else
    {
      return vsx_proto_write_command (buffer,
                                      buffer_size,

                                      VSX_PROTO_NEW_PLAYER,

                                      VSX_PROTO_TYPE_STRING,
                                      priv->room,

                                      VSX_PROTO_TYPE_STRING,
                                      priv->player_name,

                                      VSX_PROTO_TYPE_NONE);
    }
}

static int
write_keep_alive (VsxConnection *connection,
                  guint8 *buffer,
                  size_t buffer_size)
{
  return vsx_proto_write_command (buffer,
                                  buffer_size,

                                  VSX_PROTO_KEEP_ALIVE,

                                  VSX_PROTO_TYPE_NONE);
}

static void
fill_output_buffer (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  static const struct
  {
    VsxConnectionDirtyFlag flag;
    VsxConnectionWriteStateFunc func;
  } write_funcs[] =
    {
      { VSX_CONNECTION_DIRTY_FLAG_WS_HEADER, write_ws_request },
      { VSX_CONNECTION_DIRTY_FLAG_HEADER, write_header },
      { VSX_CONNECTION_DIRTY_FLAG_KEEP_ALIVE, write_keep_alive },
    };

  while (TRUE)
    {
      for (int i = 0; i < G_N_ELEMENTS (write_funcs); i++)
        {
          if (write_funcs[i].flag != 0 &&
              (priv->dirty_flags & write_funcs[i].flag) == 0)
            continue;

          int wrote = write_funcs[i].func (connection,
                                           priv->output_buffer
                                           + priv->output_length,
                                           (sizeof priv->output_buffer)
                                           - priv->output_length);

          if (wrote == -1)
            return;

          priv->dirty_flags &= ~write_funcs[i].flag;

          if (wrote == 0)
            continue;

          priv->output_length += wrote;

          goto found;
        }

      return;

    found:
      continue;
    }
}

static void
handle_write (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  fill_output_buffer (connection);

  GError *error = NULL;

  gssize wrote = g_socket_send (priv->sock,
                                (const gchar *) priv->output_buffer,
                                priv->output_length,
                                NULL, /* cancellable */
                                &error);

  if (wrote == -1)
    {
      if (error->domain != G_IO_ERROR
          || error->code != G_IO_ERROR_WOULD_BLOCK)
        {
          report_error (connection, error);
          g_error_free (error);
        }
    }
  else
    {
      /* Move any remaining data in the output buffer to the front */
      memmove (priv->output_buffer,
               priv->output_buffer + wrote,
               priv->output_length - wrote);
      priv->output_length -= wrote;

      vsx_connection_queue_keep_alive (connection);

      update_poll (connection);
    }
}

static gboolean
sock_source_cb (GIOChannel *source,
                GIOCondition condition,
                gpointer data)
{
  VsxConnection *connection = data;
  VsxConnectionPrivate *priv = connection->priv;

  if (priv->running_state == VSX_CONNECTION_RUNNING_STATE_RECONNECTING)
    {
      GError *error = NULL;

      if (!g_socket_check_connect_result (priv->sock, &error))
        {
          report_error (connection, error);
          g_error_free (error);
          return TRUE;
        }

      priv->running_state = VSX_CONNECTION_RUNNING_STATE_RUNNING;
    }

  if ((condition & (G_IO_IN |
                    G_IO_ERR |
                    G_IO_HUP)))
    handle_read (connection);
  else if ((condition & G_IO_OUT))
    handle_write (connection);
  else
    update_poll (connection);

  return TRUE;
}

static void
set_sock_condition (VsxConnection *connection,
                    GIOCondition condition)
{
  VsxConnectionPrivate *priv = connection->priv;

  if (condition == priv->sock_condition)
    return;

  if (priv->sock_source)
    g_source_remove (priv->sock_source);

  priv->sock_source = g_io_add_watch (priv->sock_channel,
                                      condition,
                                      sock_source_cb,
                                      connection);
  priv->sock_condition = condition;
}

static gboolean
has_pending_data (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  if (priv->output_length > 0)
    return TRUE;

  if (priv->dirty_flags)
    return TRUE;

  return FALSE;
}

static void
update_poll (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  GIOCondition condition;

  if (priv->running_state == VSX_CONNECTION_RUNNING_STATE_RECONNECTING)
    {
      condition = G_IO_OUT;
    }
  else
    {
      condition = G_IO_IN;

      if (has_pending_data (connection))
        condition = G_IO_OUT;
    }

  set_sock_condition (connection, condition);
}

static gboolean
vsx_connection_reconnect_cb (gpointer user_data)
{
  VsxConnection *connection = user_data;
  VsxConnectionPrivate *priv = connection->priv;

  priv->reconnect_handler = 0;

  close_socket (connection);

  GError *error = NULL;

  priv->sock = g_socket_new (G_SOCKET_FAMILY_IPV4,
                             G_SOCKET_TYPE_STREAM,
                             G_SOCKET_PROTOCOL_DEFAULT,
                             &error);

  if (priv->sock == NULL)
    {
      report_error (connection, error);
      g_error_free (error);
      return FALSE;
    }

  g_socket_set_blocking (priv->sock, FALSE);

  GInetAddress *localhost =
    g_inet_address_new_loopback (G_SOCKET_FAMILY_IPV4);
  GSocketAddress *address = g_inet_socket_address_new (localhost, 5144);

  gboolean connect_ret = g_socket_connect (priv->sock,
                                           address,
                                           NULL, /* cancellable */
                                           &error);

  g_object_unref (address);
  g_object_unref (localhost);

  GIOCondition condition;

  if (connect_ret)
    {
      priv->running_state = VSX_CONNECTION_RUNNING_STATE_RUNNING;
      condition = G_IO_IN | G_IO_OUT;
    }
  else if (error->domain == G_IO_ERROR
           && error->code == G_IO_ERROR_PENDING)
    {
      g_error_free (error);
      priv->running_state = VSX_CONNECTION_RUNNING_STATE_RECONNECTING;
      condition = G_IO_OUT;
    }
  else
    {
      report_error (connection, error);
      g_error_free (error);
      return FALSE;
    }

  priv->dirty_flags |= (VSX_CONNECTION_DIRTY_FLAG_WS_HEADER
                        | VSX_CONNECTION_DIRTY_FLAG_HEADER);
  priv->ws_terminator_pos = 0;

  priv->sock_channel = g_io_channel_unix_new (g_socket_get_fd (priv->sock));

  g_io_channel_set_encoding (priv->sock_channel,
                             NULL /* encoding */,
                             NULL /* error */);
  g_io_channel_set_buffered (priv->sock_channel, FALSE);

  set_sock_condition (connection, condition);

  /* Remove the handler */
  return FALSE;
}

static void
vsx_connection_queue_reconnect (VsxConnection *connection)
{
  VsxConnectionPrivate *priv = connection->priv;

  g_assert (priv->reconnect_handler == 0);

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

          priv->running_state =
            VSX_CONNECTION_RUNNING_STATE_WAITING_FOR_RECONNECT;

          g_assert (priv->reconnect_handler == 0);
          priv->reconnect_handler = g_idle_add (vsx_connection_reconnect_cb,
                                                connection);
        }
    }
  else
    {
      switch (priv->running_state)
        {
        case VSX_CONNECTION_RUNNING_STATE_DISCONNECTED:
          /* already disconnected */
          break;

        case VSX_CONNECTION_RUNNING_STATE_RECONNECTING:
        case VSX_CONNECTION_RUNNING_STATE_RUNNING:
          priv->running_state =
            VSX_CONNECTION_RUNNING_STATE_DISCONNECTED;
          close_socket (connection);
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
