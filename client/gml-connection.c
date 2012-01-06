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

#include <glib-object.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <string.h>

#include "gml-connection.h"
#include "gml-marshal.h"
#include "gml-enum-types.h"

enum
{
  PROP_0,

  PROP_SERVER_BASE_URL,
  PROP_SOUP_SESSION,
  PROP_ROOM,
  PROP_RUNNING,
  PROP_STRANGER_TYPING,
  PROP_STATE
};

enum
{
  SIGNAL_GOT_ERROR,
  SIGNAL_MESSAGE,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

static void
gml_connection_dispose (GObject *object);

static void
gml_connection_finalize (GObject *object);

static void
gml_connection_queue_message (GmlConnection *connection);

G_DEFINE_TYPE (GmlConnection, gml_connection, G_TYPE_OBJECT);

#define GML_CONNECTION_GET_PRIVATE(obj) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GML_TYPE_CONNECTION, \
                                GmlConnectionPrivate))

/* Initial timeout (in seconds) before attempting to reconnect after
   an error. The timeout will be doubled every time there is a
   failure */
#define GML_CONNECTION_INITIAL_TIMEOUT 16

/* If the timeout reaches this maximum then it won't be doubled further */
#define GML_CONNECTION_MAX_TIMEOUT 512

typedef enum
{
  GML_CONNECTION_RUNNING_STATE_DISCONNECTED,
  GML_CONNECTION_RUNNING_STATE_RUNNING,
  GML_CONNECTION_RUNNING_STATE_WAITING_FOR_RECONNECT
} GmlConnectionRunningState;

struct _GmlConnectionPrivate
{
  char *server_base_url;
  char *room;
  SoupSession *soup_session;
  SoupMessage *message;
  GString *line_buffer;
  gboolean has_disconnected;
  JsonParser *json_parser;
  guint reconnect_timeout;
  guint reconnect_handler;
  gint64 num;
  char *person_id;
  GmlConnectionRunningState running_state;
  GmlConnectionState state;
  gboolean stranger_typing;
  int next_message_num;
  int latest_message;
};

static void
gml_connection_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
  GmlConnection *connection = GML_CONNECTION (object);

  switch (prop_id)
    {
    case PROP_RUNNING:
      g_value_set_boolean (value, gml_connection_get_running (connection));
      break;

    case PROP_STRANGER_TYPING:
      g_value_set_boolean (value,
                           gml_connection_get_stranger_typing (connection));
      break;

    case PROP_STATE:
      g_value_set_enum (value,
                        gml_connection_get_state (connection));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gml_connection_set_property (GObject *object,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
  GmlConnection *connection = GML_CONNECTION (object);
  GmlConnectionPrivate *priv = connection->priv;

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

static void
gml_connection_set_stranger_typing (GmlConnection *connection,
                                    gboolean typing)
{
  GmlConnectionPrivate *priv = connection->priv;

  if (priv->stranger_typing != typing)
    {
      priv->stranger_typing = typing;
      g_object_notify (G_OBJECT (connection), "stranger-typing");
    }
}

static void
gml_connection_set_state (GmlConnection *connection,
                          GmlConnectionState state)
{
  GmlConnectionPrivate *priv = connection->priv;

  if (priv->state != state)
    {
      priv->state = state;
      g_object_notify (G_OBJECT (connection), "state");
    }
}

static gboolean
handle_message (GmlConnection *connection,
                JsonNode *object,
                GError **error)
{
  GmlConnectionPrivate *priv = connection->priv;
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

      if (json_array_get_length (array) < 2)
        goto bad_data;

      header_node = json_array_get_element (array, 1);

      if (json_node_get_node_type (header_node) != JSON_NODE_OBJECT)
        goto bad_data;

      header_object = json_node_get_object (header_node);

      if (!get_object_int_member (header_object, "num", &priv->num)
          || !get_object_string_member (header_object, "id", &person_id))
        goto bad_data;

      g_free (priv->person_id);
      priv->person_id = g_strdup (person_id);
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

      if (!get_object_int_member (message_object, "person", &num)
          || !get_object_string_member (message_object, "text", &text))
        goto bad_data;

      /* Silently drop messages that we've already received */
      if (priv->latest_message < priv->next_message_num)
        {
          g_signal_emit (connection,
                         signals[SIGNAL_MESSAGE],
                         0, /* detail */
                         num == priv->num
                         ? GML_CONNECTION_PERSON_YOU
                         : GML_CONNECTION_PERSON_STRANGER,
                         text);
          priv->latest_message = priv->next_message_num;
        }

      priv->next_message_num++;
    }
  else if (!strcmp (method_string, "state"))
    {
      JsonNode *state_node;
      const char *state;

      if (json_array_get_length (array) < 2)
        goto bad_data;

      state_node = json_array_get_element (array, 1);

      if (json_node_get_node_type (state_node) != JSON_NODE_VALUE)
        goto bad_data;

      state = json_node_get_string (state_node);

      if (!strcmp (state, "in-progress"))
        gml_connection_set_state (connection,
                                  GML_CONNECTION_STATE_IN_PROGRESS);
      else if (!strcmp (state, "done"))
        gml_connection_set_state (connection,
                                  GML_CONNECTION_STATE_DONE);
      else
        gml_connection_set_state (connection,
                                  GML_CONNECTION_STATE_AWAITING_PARTNER);
    }
  else if (!strcmp (method_string, "typing"))
    gml_connection_set_stranger_typing (connection, TRUE);
  else if (!strcmp (method_string, "not-typing"))
    gml_connection_set_stranger_typing (connection, FALSE);

  return TRUE;

 bad_data:
  g_set_error (error,
               GML_CONNECTION_ERROR,
               GML_CONNECTION_ERROR_BAD_DATA,
               "Bad data received from the server");
  return FALSE;
}

static void
gml_connection_signal_error (GmlConnection *connection,
                             GError *error)
{
  g_signal_emit (connection,
                 signals[SIGNAL_GOT_ERROR],
                 0, /* detail */
                 error);
}

static void
gml_connection_process_lines (GmlConnection *connection)
{
  GmlConnectionPrivate *priv = connection->priv;
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

              gml_connection_signal_error (connection, error);

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
gml_connection_got_chunk_cb (SoupMessage *message,
                             SoupBuffer *chunk,
                             GmlConnection *connection)
{
  GmlConnectionPrivate *priv = connection->priv;

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
      gml_connection_process_lines (connection);
    }
}

static void
gml_connection_got_headers_cb (SoupMessage *message,
                               GmlConnection *connection)
{
  GmlConnectionPrivate *priv = connection->priv;

  /* Every time we get a successful connection we'll reset the
     reconnect timeout */
  if (message->status_code == SOUP_STATUS_OK)
    {
      priv->reconnect_timeout = GML_CONNECTION_INITIAL_TIMEOUT;

      g_string_set_size (priv->line_buffer, 0);
    }
}

static gboolean
gml_connection_reconnect_cb (gpointer user_data)
{
  GmlConnection *connection = user_data;

  /* Queue a reconnect. This will switch back to the running state */
  gml_connection_queue_message (connection);

  /* Remove the handler */
  return FALSE;
}

static void
gml_connection_queue_reconnect (GmlConnection *connection)
{
  GmlConnectionPrivate *priv = connection->priv;

  priv->reconnect_handler =
    g_timeout_add_seconds (priv->reconnect_timeout,
                           gml_connection_reconnect_cb,
                           connection);
  /* Next time we need to try to reconnect we'll delay for twice
     as long, up to the maximum timeout */
  priv->reconnect_timeout *= 2;
  if (priv->reconnect_timeout > GML_CONNECTION_MAX_TIMEOUT)
    priv->reconnect_timeout = GML_CONNECTION_MAX_TIMEOUT;

  priv->running_state = GML_CONNECTION_RUNNING_STATE_WAITING_FOR_RECONNECT;
}

static void
gml_connection_message_completed_cb (SoupSession *soup_session,
                                     SoupMessage *message,
                                     gpointer user_data)
{
  GmlConnection *connection = user_data;
  GmlConnectionPrivate *priv = connection->priv;

  /* If the message was cancelled then we'll assume these came from a
     request to stop running so we'll switch to the disconnected
     state */
  if (message->status_code == SOUP_STATUS_CANCELLED)
    priv->running_state = GML_CONNECTION_RUNNING_STATE_DISCONNECTED;
  /* If the connection just ended without an error then we'll try to
     reconnect immediately */
  else
    {
      GError *error;

      gml_connection_queue_reconnect (connection);

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

      gml_connection_signal_error (connection, error);

      g_error_free (error);
    }
}

static void
gml_connection_queue_message (GmlConnection *connection)
{
  GmlConnectionPrivate *priv = connection->priv;
  GString *url;
  const char *param;
  char *encoded_param;

  url = g_string_new (priv->server_base_url);

  if (url->len == 0 || url->str[url->len - 1] != '/')
    g_string_append_c (url, '/');

  if (priv->person_id)
    {
      g_string_append (url, "watch_person?");
      param = priv->person_id;
    }
  else
    {
      g_string_append (url, "new_person?");
      param = priv->room;
    }

  encoded_param = soup_uri_encode (param, NULL);
  g_string_append (url, encoded_param);
  g_free (encoded_param);

  priv->message = soup_message_new ("GET", url->str);

  g_string_free (url, TRUE);

  g_signal_connect (priv->message, "got-headers",
                    G_CALLBACK (gml_connection_got_headers_cb),
                    connection);

  g_signal_connect (priv->message, "got-chunk",
                    G_CALLBACK (gml_connection_got_chunk_cb),
                    connection);

  /* The server will resend all of the messages in the conversation so
     we want to start counting from 0 again. All messages before
     priv->latest_message will be silently dropped */
  priv->next_message_num = 0;

  soup_session_queue_message (priv->soup_session,
                              priv->message,
                              gml_connection_message_completed_cb,
                              connection);

  priv->running_state = GML_CONNECTION_RUNNING_STATE_RUNNING;
}

static void
gml_connection_constructed (GObject *object)
{
  GmlConnection *connection = GML_CONNECTION (object);
  GmlConnectionPrivate *priv = connection->priv;

  if (priv->soup_session == NULL)
    priv->soup_session = soup_session_async_new ();
}

static void
gml_connection_set_running_internal (GmlConnection *connection,
                                     gboolean running)
{
  GmlConnectionPrivate *priv = connection->priv;

  if (running)
    {
      if (priv->running_state == GML_CONNECTION_RUNNING_STATE_DISCONNECTED)
        {
          /* Reset the retry timeout because this is a first attempt
             at connecting */
          priv->reconnect_timeout = GML_CONNECTION_INITIAL_TIMEOUT;
          gml_connection_queue_message (connection);
        }
    }
  else
    {
      /* Mark that we've disconnected so that if we're in the middle
         of processing lines we'll bail out */
      priv->has_disconnected = TRUE;

      switch (priv->running_state)
        {
        case GML_CONNECTION_RUNNING_STATE_DISCONNECTED:
          /* already disconnected */
          break;

        case GML_CONNECTION_RUNNING_STATE_RUNNING:
          /* This will also set the disconnected state */
          soup_session_cancel_message (priv->soup_session,
                                       priv->message,
                                       SOUP_STATUS_CANCELLED);
          break;

        case GML_CONNECTION_RUNNING_STATE_WAITING_FOR_RECONNECT:
          /* Cancel the timeout */
          g_source_remove (priv->reconnect_handler);
          priv->running_state = GML_CONNECTION_RUNNING_STATE_DISCONNECTED;
          break;
        }
    }
}

void
gml_connection_set_running (GmlConnection *connection,
                            gboolean running)
{
  g_return_if_fail (GML_IS_CONNECTION (connection));

  gml_connection_set_running_internal (connection, running);

  g_object_notify (G_OBJECT (connection), "running");
}

gboolean
gml_connection_get_running (GmlConnection *connection)
{
  g_return_val_if_fail (GML_IS_CONNECTION (connection), FALSE);

  return (connection->priv->running_state
          != GML_CONNECTION_RUNNING_STATE_DISCONNECTED);
}

static void
gml_connection_class_init (GmlConnectionClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GParamSpec *pspec;

  gobject_class->dispose = gml_connection_dispose;
  gobject_class->finalize = gml_connection_finalize;
  gobject_class->constructed = gml_connection_constructed;
  gobject_class->set_property = gml_connection_set_property;
  gobject_class->get_property = gml_connection_get_property;

  pspec = g_param_spec_string ("server-base-url",
                               "Server base URL",
                               "The base URL of the server to connect to",
                               "http://gemelo.org:5142/",
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

  pspec = g_param_spec_boolean ("stranger-typing",
                                "Stranger typing",
                                "Whether the other person in the conversation "
                                "is typing.",
                                FALSE,
                                G_PARAM_READABLE
                                | G_PARAM_STATIC_NAME
                                | G_PARAM_STATIC_NICK
                                | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (gobject_class, PROP_STRANGER_TYPING, pspec);

  pspec = g_param_spec_enum ("state",
                             "State",
                             "State of the conversation",
                             GML_TYPE_CONNECTION_STATE,
                             GML_CONNECTION_STATE_AWAITING_PARTNER,
                             G_PARAM_READABLE
                             | G_PARAM_STATIC_NAME
                             | G_PARAM_STATIC_NICK
                             | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (gobject_class, PROP_STATE, pspec);

  signals[SIGNAL_GOT_ERROR] =
    g_signal_new ("got-error",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GmlConnectionClass, got_error),
                  NULL, /* accumulator */
                  NULL, /* accumulator data */
                  gml_marshal_VOID__BOXED,
                  G_TYPE_NONE,
                  1, /* num arguments */
                  G_TYPE_ERROR);

  signals[SIGNAL_MESSAGE] =
    g_signal_new ("message",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GmlConnectionClass, message),
                  NULL, /* accumulator */
                  NULL, /* accumulator data */
                  gml_marshal_VOID__ENUM_STRING,
                  G_TYPE_NONE,
                  2, /* num arguments */
                  GML_TYPE_CONNECTION_PERSON,
                  G_TYPE_STRING);

  g_type_class_add_private (klass, sizeof (GmlConnectionPrivate));
}

static void
gml_connection_init (GmlConnection *self)
{
  GmlConnectionPrivate *priv;

  priv = self->priv = GML_CONNECTION_GET_PRIVATE (self);

  priv->line_buffer = g_string_new (NULL);
  priv->next_message_num = 0;
  priv->latest_message = -1;
}

static void
gml_connection_dispose (GObject *object)
{
  GmlConnection *self = (GmlConnection *) object;
  GmlConnectionPrivate *priv = self->priv;

  gml_connection_set_running_internal (self, FALSE);

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

  G_OBJECT_CLASS (gml_connection_parent_class)->dispose (object);
}

static void
gml_connection_finalize (GObject *object)
{
  GmlConnection *self = (GmlConnection *) object;
  GmlConnectionPrivate *priv = self->priv;

  g_free (priv->server_base_url);
  g_free (priv->room);
  g_free (priv->person_id);

  g_string_free (priv->line_buffer, TRUE);

  G_OBJECT_CLASS (gml_connection_parent_class)->finalize (object);
}

GmlConnection *
gml_connection_new (const char *server_base_url,
                    const char *room)
{
  GmlConnection *self = g_object_new (GML_TYPE_CONNECTION,
                                      "server-base-url", server_base_url,
                                      "room", room,
                                      NULL);

  return self;
}

gboolean
gml_connection_get_stranger_typing (GmlConnection *connection)
{
  GmlConnectionPrivate *priv = connection->priv;

  return priv->stranger_typing;
}

GmlConnectionState
gml_connection_get_state (GmlConnection *connection)
{
  GmlConnectionPrivate *priv = connection->priv;

  return priv->state;
}

GQuark
gml_connection_error_quark (void)
{
  return g_quark_from_static_string ("gml-connection-error-quark");
}
