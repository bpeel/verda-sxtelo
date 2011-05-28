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
#include <string.h>

#include "gml-server.h"
#include "gml-main-context.h"
#include "gml-http-parser.h"
#include "gml-person-set.h"
#include "gml-new-person-response.h"
#include "gml-string-response.h"
#include "gml-conversation.h"

struct _GmlServer
{
  GmlMainContext *main_context;
  GmlMainContextSource *server_socket_source;
  GmlMainContextSource *quit_source;
  GSocket *server_socket;

  /* If this gets set then gml_server_run will return and report the
     error */
  GError *fatal_error;

  gboolean quit_received;

  /* List of open connections */
  GList *connections;

  /* Hash table of pending conversations. This only contains
     conversations that only have one person. The key is the name of
     the room and the value is the a GmlServerConversationHashData
     struct (which contains a pointer to the conversation). The hash
     table only takes a weak reference on the conversation so we can
     detect if the person leaves before a second person joins and just
     destroy the conversation. The data for the weak reference is the
     same pointer to the hash data */
  GHashTable *pending_conversations;

  GmlPersonSet *person_set;
};

#define GML_SERVER_OUTPUT_BUFFER_SIZE 1024

typedef struct
{
  GmlServer *server;

  GSocket *client_socket;
  GmlMainContextSource *source;

  /* Pointer to the GList node in the connections list so it can be
     removed quickly */
  GList *list_node;

  GmlHttpParser http_parser;

  /* This becomes TRUE when we've received something from the client
     that we don't understand and we're ignoring any further input */
  gboolean had_bad_input;
  /* This becomes TRUE when the client has closed its end of the
     connection */
  gboolean read_finished;
  /* This becomes TRUE when we've stopped writing data. This will only
     happen after the client closes its connection or we've had bad
     input and we're ignoring further data */
  gboolean write_finished;

  /* The next response that will be added to the response queue when a
     complete request is received */
  GmlResponse *next_response;

  /* Queue of GmlResponses to send to this client */
  GQueue response_queue;

  unsigned int output_length;
  guint8 output_buffer[GML_SERVER_OUTPUT_BUFFER_SIZE];
} GmlServerConnection;

typedef struct
{
  char *room_name;
  GmlServer *server;
  GmlConversation *conversation;
} GmlServerConversationHashData;

static void
free_conversation_hash_data (GmlServerConversationHashData *data)
{
  g_free (data->room_name);
  g_slice_free (GmlServerConversationHashData, data);
}

static void
conversation_weak_ref_cb (gpointer user_data,
                          GObject *where_the_object_was)
{
  GmlServerConversationHashData *data = user_data;

  /* This will also destroy the hash data */
  g_hash_table_remove (data->server->pending_conversations,
                       data->room_name);
}

static GmlConversation *
get_conversation (GmlServer *server,
                  const char *room_name)
{
  GmlServerConversationHashData *data;
  GmlConversation *conversation;

  if ((data = g_hash_table_lookup (server->pending_conversations,
                                   room_name)) == NULL)
    {
      /* If there's no conversation with that name then we'll create it */
      conversation = gml_conversation_new ();

      data = g_slice_new (GmlServerConversationHashData);

      data->room_name = g_strdup (room_name);
      data->server = server;
      data->conversation = conversation;

      /* Take a weak reference on the conversation so we can remove it
         from the pending conversation list if the first person
         disappears before another person joins */
      g_object_weak_ref (G_OBJECT (conversation),
                         conversation_weak_ref_cb,
                         data);

      g_hash_table_insert (server->pending_conversations,
                           data->room_name,
                           data);
    }
  else
    {
      conversation = g_object_ref (data->conversation);

      g_object_weak_unref (G_OBJECT (conversation),
                           conversation_weak_ref_cb,
                           data);

      /* This should also free the data */
      g_hash_table_remove (server->pending_conversations, room_name);
    }

  return conversation;
}

static GmlResponse *
parse_new_person_request (GmlServerConnection *connection,
                          const char *query_string)
{
  GmlConversation *conversation;
  GSocketAddress *address;
  GmlPerson *person;
  GmlResponse *response;
  const char *p;

  /* The query string will be used as the room name. It should only
     contain letters */
  if (query_string == NULL || *query_string == 0)
    goto bad_room_name;

  for (p = query_string; *p; p++)
    if (!g_ascii_isalpha (*p))
      goto bad_room_name;

  address = g_socket_get_remote_address (connection->client_socket, NULL);
  conversation = get_conversation (connection->server, query_string);
  person = gml_person_set_generate_person (connection->server->person_set,
                                           address,
                                           conversation);
  g_object_unref (conversation);
  if (address)
    g_object_unref (address);

  response = gml_new_person_response_new (person);

  g_object_unref (person);

  return response;

 bad_room_name:
  return gml_string_response_new (GML_STRING_RESPONSE_BAD_REQUEST);
}

static const struct
{
  const char *method;
  const char *url;
  GmlResponse * (* parse_func) (GmlServerConnection *connection,
                                const char *query_string);
}
requests[] =
  {
    { "GET", "/new_person", parse_new_person_request }
  };

static gboolean
gml_server_request_line_received_cb (const char *method,
                                     const char *uri,
                                     void *user_data)
{
  GmlServerConnection *connection = user_data;
  const char *query_string;
  const char *question_mark;
  const char *url;
  char *url_copy = NULL;
  int i;

  if ((question_mark = strchr (uri, '?')))
    {
      url_copy = strndup (uri, question_mark - uri);
      url = url_copy;
      query_string = question_mark + 1;
    }
  else
    {
      url = uri;
      query_string = NULL;
    }

  for (i = 0; i < G_N_ELEMENTS (requests); i++)
    if (!strcmp (url, requests[i].url))
      {
        if (strcmp (method, requests[i].method))
          connection->next_response =
            gml_string_response_new (GML_STRING_RESPONSE_UNSUPPORTED_REQUEST);
        else
          connection->next_response =
            requests[i].parse_func (connection, query_string);

        goto done;
      }

  if (!strcmp (method, "GET") || !strcmp (method, "POST"))
    connection->next_response =
      gml_string_response_new (GML_STRING_RESPONSE_NOT_FOUND);
  else
    connection->next_response =
      gml_string_response_new (GML_STRING_RESPONSE_UNSUPPORTED_REQUEST);

 done:
  g_free (url_copy);

  return TRUE;
}

static gboolean
gml_server_header_received_cb (const char *field_name,
                               const char *value,
                               void *user_data)
{
  return TRUE;
}

static gboolean
gml_server_data_received_cb (const guint8 *data,
                             unsigned int length,
                             void *user_data)
{
  return TRUE;
}

static gboolean
gml_server_request_finished_cb (void *user_data)
{
  GmlServerConnection *connection = user_data;

  /* We've successfully got a complete request so we'll queue the
     response */
  g_queue_push_tail (&connection->response_queue, connection->next_response);
  connection->next_response = NULL;

  return TRUE;
}

static GmlHttpParserVtable
gml_server_http_parser_vtable =
  {
    .request_line_received = gml_server_request_line_received_cb,
    .header_received = gml_server_header_received_cb,
    .data_received = gml_server_data_received_cb,
    .request_finished = gml_server_request_finished_cb
  };

static void
gml_server_quit_cb (GmlMainContextSource *source,
                    void *user_data)
{
  GmlServer *server = user_data;

  server->quit_received = TRUE;
}

static void
gml_server_connection_clear_responses (GmlServerConnection *connection)
{
  g_queue_foreach (&connection->response_queue,
                   (GFunc) g_object_unref,
                   NULL);
  g_queue_clear (&connection->response_queue);

  if (connection->next_response)
    {
      g_object_unref (connection->next_response);
      connection->next_response = NULL;
    }
}

static void
set_bad_input (GmlServerConnection *connection, GError *error)
{
  GmlResponse *response;

  /* Replace all of the queued responses with an error response */
  gml_server_connection_clear_responses (connection);

  if (error->domain == GML_HTTP_PARSER_ERROR
      && error->code == GML_HTTP_PARSER_ERROR_UNSUPPORTED)
    response =
      gml_string_response_new (GML_STRING_RESPONSE_UNSUPPORTED_REQUEST);
  else
    response = gml_string_response_new (GML_STRING_RESPONSE_BAD_REQUEST);

  g_queue_push_tail (&connection->response_queue, response);

  connection->had_bad_input = TRUE;
}

static void
gml_server_remove_connection (GmlServer *server,
                              GmlServerConnection *connection)
{
  gml_server_connection_clear_responses (connection);

  gml_main_context_remove_source (server->main_context, connection->source);
  g_object_unref (connection->client_socket);
  server->connections = g_list_delete_link (server->connections,
                                            connection->list_node);
  g_slice_free (GmlServerConnection, connection);
}

static void
update_poll (GmlServerConnection *connection)
{
  GmlMainContextPollFlags flags = 0;

  if (!connection->read_finished)
    flags |= GML_MAIN_CONTEXT_POLL_IN;

  /* Shutdown the socket if we've finished writing */
  if (!connection->write_finished
      && (connection->read_finished || connection->had_bad_input)
      && g_queue_is_empty (&connection->response_queue)
      && connection->output_length == 0)
    {
      GError *error = NULL;

      if (!g_socket_shutdown (connection->client_socket,
                              FALSE, /* shutdown_read */
                              TRUE, /* shutdown_write */
                              &error))
        {
          g_print ("shutdown socket failed: %s\n", error->message);
          g_clear_error (&error);
          gml_server_remove_connection (connection->server, connection);
          return;
        }

      connection->write_finished = TRUE;
    }

  if (!connection->write_finished
      && (!g_queue_is_empty (&connection->response_queue)
          || connection->output_length > 0))
    flags |= GML_MAIN_CONTEXT_POLL_OUT;

  /* If both ends of the connection are closed then we can abandon
     this connectin */
  if (connection->read_finished && connection->write_finished)
    gml_server_remove_connection (connection->server, connection);
  else
    gml_main_context_modify_poll (connection->server->main_context,
                                  connection->source,
                                  flags);
}

static void
gml_server_connection_poll_cb (GmlMainContextSource *source,
                               int fd,
                               GmlMainContextPollFlags flags,
                               void *user_data)
{
  GmlServerConnection *connection = user_data;
  GmlServer *server = connection->server;
  char buf[1024];

  if (flags & GML_MAIN_CONTEXT_POLL_IN)
    {
      GError *error = NULL;

      gssize got =
        g_socket_receive (connection->client_socket,
                          buf,
                          sizeof (buf),
                          NULL, /* cancellable */
                          &error);

      /* FIXME, the g_prints should be in some kind of logging mechanism */

      if (got == 0)
        {
          if (!connection->had_bad_input
              && !gml_http_parser_parser_eof (&connection->http_parser,
                                              &error))
            {
              set_bad_input (connection, error);
              g_clear_error (&error);
            }

          connection->read_finished = TRUE;

          update_poll (connection);
        }
      else if (got == -1)
        {
          if (error->domain != G_IO_ERROR
              || error->code != G_IO_ERROR_WOULD_BLOCK)
            {
              g_print ("Error reading from socket: %s\n", error->message);
              gml_server_remove_connection (server, connection);
            }

          g_clear_error (&error);
        }
      else
        {
          if (!connection->had_bad_input
              && !gml_http_parser_parse_data (&connection->http_parser,
                                              (guint8 *) buf,
                                              got,
                                              &error))
            {
              set_bad_input (connection, error);
              g_clear_error (&error);
            }

          update_poll (connection);
        }
    }
  else if (flags & GML_MAIN_CONTEXT_POLL_OUT)
    {
      GError *error = NULL;
      gssize wrote;

      /* Try to fill the output buffer as much as possible before
         initiating a write */
      while (connection->output_length < GML_SERVER_OUTPUT_BUFFER_SIZE
             && !g_queue_is_empty (&connection->response_queue))
        {
          GmlResponse *response =
            g_queue_peek_head (&connection->response_queue);
          unsigned int added;

          added =
            gml_response_add_data (response,
                                   connection->output_buffer
                                   + connection->output_length,
                                   GML_SERVER_OUTPUT_BUFFER_SIZE
                                   - connection->output_length);

          connection->output_length += added;

          /* If the response is now finished then remove it from the queue */
          if (gml_response_is_finished (response))
            {
              g_object_unref (response);
              g_queue_pop_head (&connection->response_queue);
            }
        }

      if ((wrote = g_socket_send (connection->client_socket,
                                  (const gchar *) connection->output_buffer,
                                  connection->output_length,
                                  NULL,
                                  &error)) == -1)
        {
          if (error->domain != G_IO_ERROR
              || error->code != G_IO_ERROR_WOULD_BLOCK)
            {
              g_print ("Error writing to socket: %s\n", error->message);
              gml_server_remove_connection (server, connection);
            }

          g_clear_error (&error);
        }
      else
        {
          /* Move any remaining data in the output buffer to the front */
          memmove (connection->output_buffer,
                   connection->output_buffer + wrote,
                   connection->output_length - wrote);
          connection->output_length -= wrote;

          update_poll (connection);
        }
    }
}

static void
gml_server_pending_connection_cb (GmlMainContextSource *source,
                                  int fd,
                                  GmlMainContextPollFlags flags,
                                  void *user_data)
{
  GmlServer *server = user_data;
  GSocket *client_socket;
  GError *error = NULL;

  client_socket = g_socket_accept (server->server_socket,
                                   NULL,
                                   &error);

  if (client_socket == NULL)
    {
      /* Ignore WOULD_BLOCK errors */
      if (error->domain == G_IO_ERROR
          && error->code == G_IO_ERROR_WOULD_BLOCK)
        g_clear_error (&error);
      else
        /* This will cause gml_server_run to return */
        server->fatal_error = error;
    }
  else
    {
      GmlServerConnection *connection;

      g_socket_set_blocking (client_socket, FALSE);

      connection = g_slice_new (GmlServerConnection);

      connection->server = server;
      connection->client_socket = client_socket;
      connection->source =
        gml_main_context_add_poll (server->main_context,
                                   g_socket_get_fd (client_socket),
                                   GML_MAIN_CONTEXT_POLL_IN,
                                   gml_server_connection_poll_cb,
                                   connection);
      server->connections = g_list_prepend (server->connections,
                                            connection);

      gml_http_parser_init (&connection->http_parser,
                            &gml_server_http_parser_vtable,
                            connection);

      connection->next_response = NULL;
      g_queue_init (&connection->response_queue);

      connection->had_bad_input = FALSE;
      connection->read_finished = FALSE;
      connection->write_finished = FALSE;

      connection->output_length = 0;

      /* Store the list node so we can quickly remove the connection
         from the list */
      connection->list_node = server->connections;
    }
}

GmlServer *
gml_server_new (GSocketAddress *address,
                GError **error)
{
  GmlServer *server;

  g_return_val_if_fail (G_IS_SOCKET_ADDRESS (address), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  server = g_new0 (GmlServer, 1);

  server->main_context = gml_main_context_new (error);

  if (server->main_context == NULL)
    goto error;

  server->server_socket = g_socket_new (g_socket_address_get_family (address),
                                        G_SOCKET_TYPE_STREAM,
                                        G_SOCKET_PROTOCOL_DEFAULT,
                                        error);

  g_socket_set_blocking (server->server_socket, FALSE);

  if (server->server_socket == NULL)
    goto error;

  if (!g_socket_bind (server->server_socket,
                      address,
                      TRUE,
                      error))
    goto error;

  if (!g_socket_listen (server->server_socket,
                        error))
    goto error;

  server->person_set = gml_person_set_new ();

  /* The hash table doesn't a destroy function for the key because its
     owned by the hash data */
  server->pending_conversations =
    g_hash_table_new_full (g_str_hash,
                           g_str_equal,
                           NULL, /* key_destroy */
                           /* value_destroy */
                           (GDestroyNotify) free_conversation_hash_data);

  server->server_socket_source =
    gml_main_context_add_poll (server->main_context,
                               g_socket_get_fd (server->server_socket),
                               GML_MAIN_CONTEXT_POLL_IN,
                               gml_server_pending_connection_cb,
                               server);

  server->quit_source =
    gml_main_context_add_quit (server->main_context,
                               gml_server_quit_cb,
                               server);

  return server;

 error:
  if (server->main_context)
    gml_main_context_free (server->main_context);

  if (server->server_socket)
    g_object_unref (server->server_socket);

  g_free (server);

  return NULL;
}

gboolean
gml_server_run (GmlServer *server,
                GError **error)
{
  server->quit_received = FALSE;

  do
    gml_main_context_poll (server->main_context, -1);
  while (server->fatal_error == NULL
         && !server->quit_received);

  if (server->fatal_error)
    {
      g_propagate_error (error, server->fatal_error);
      server->fatal_error = NULL;

      return FALSE;
    }
  else
    return TRUE;
}

void
gml_server_free (GmlServer *server)
{
  while (server->connections)
    gml_server_remove_connection (server, server->connections->data);

  gml_person_set_free (server->person_set);

  /* Destroying all of the people should make all of the conversations
     disappear so we don't need to bother removing all of the weak
     references */
  g_warn_if_fail (g_hash_table_size (server->pending_conversations) == 0);
  g_hash_table_destroy (server->pending_conversations);

  gml_main_context_remove_source (server->main_context,
                                  server->quit_source);

  gml_main_context_remove_source (server->main_context,
                                  server->server_socket_source);
  gml_main_context_free (server->main_context);

  g_object_unref (server->server_socket);

  g_free (server);
}
