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
#include <sys/socket.h>
#include <errno.h>

#include "gml-server.h"
#include "gml-main-context.h"
#include "gml-http-parser.h"
#include "gml-person-set.h"
#include "gml-string-response.h"
#include "gml-conversation.h"
#include "gml-conversation-set.h"
#include "gml-new-person-handler.h"
#include "gml-leave-handler.h"
#include "gml-send-message-handler.h"
#include "gml-watch-person-handler.h"
#include "gml-start-typing-handler.h"
#include "gml-stop-typing-handler.h"
#include "gml-log.h"

struct _GmlServer
{
  GmlMainContextSource *server_socket_source;
  GSocket *server_socket;

  /* If this gets set then gml_server_run will return and report the
     error */
  GError *fatal_error;

  /* List of open connections */
  GList *connections;

  GmlConversationSet *pending_conversations;

  GmlPersonSet *person_set;

  gint64 last_gc_time;
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

  /* The current request handler or NULL if we couldn't find an
     appropriate one when the request line was received */
  GmlRequestHandler *current_request_handler;

  /* Queue of GmlResponses to send to this client */
  GQueue response_queue;

  unsigned int output_length;
  guint8 output_buffer[GML_SERVER_OUTPUT_BUFFER_SIZE];

  /* IP address of the connection. This is only filled in if logging
     is enabled */
  char *peer_address_string;

  /* Time since the response queue became empty. The connection will
   * be removed if this stays empty for too long */
  gint64 no_response_age;
} GmlServerConnection;

/* Interval time in micro-seconds to run the dead person garbage
   collector */
#define GML_SERVER_GC_TIMEOUT (5 * 60 * (gint64) 1000000)

/* Time in microseconds after which a connection with no responses
 * will be considered dead. This is necessary to avoid keeping around
 * connections that open the socket and then don't send any
 * data. These would otherwise hang out indefinitely and use up
 * resources. */
#define GML_SERVER_NO_RESPONSE_TIMEOUT (5 * 60 * (gint64) 1000000)

static const struct
{
  const char *url;
  GType (* get_type_func) (void);
}
requests[] =
  {
    { "/start_typing", gml_start_typing_handler_get_type },
    { "/stop_typing", gml_stop_typing_handler_get_type },
    { "/send_message", gml_send_message_handler_get_type },
    { "/watch_person", gml_watch_person_handler_get_type },
    { "/new_person", gml_new_person_handler_get_type },
    { "/leave", gml_leave_handler_get_type }
  };

static void
update_poll (GmlServerConnection *connection);

static void
gml_server_remove_connection (GmlServer *server,
                              GmlServerConnection *connection);

static void
response_changed_cb (GmlResponse *response,
                     GmlServerConnection *connection)
{
  if (response == g_queue_peek_head (&connection->response_queue))
    update_poll (connection);
}

static gboolean
gml_server_request_line_received_cb (const char *method_str,
                                     const char *uri,
                                     void *user_data)
{
  GmlServerConnection *connection = user_data;
  const char *query_string;
  const char *question_mark;
  const char *url;
  GmlRequestHandler *handler;
  GmlRequestMethod method;
  char *url_copy = NULL;
  int i;

  g_warn_if_fail (connection->current_request_handler == NULL);

  if (!strcmp (method_str, "GET"))
    method = GML_REQUEST_METHOD_GET;
  else if (!strcmp (method_str, "POST"))
    method = GML_REQUEST_METHOD_POST;
  else if (!strcmp (method_str, "OPTIONS"))
    method = GML_REQUEST_METHOD_OPTIONS;
  else
    method = GML_REQUEST_METHOD_UNKNOWN;

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
        handler = g_object_new (requests[i].get_type_func (), NULL);

        goto got_handler;
      }

  /* If we didn't find a handler then construct a default handler
     which will report an error */
  handler = gml_request_handler_new ();

 got_handler:
  handler->socket_address =
    g_socket_get_remote_address (connection->client_socket, NULL);
  handler->conversation_set =
    g_object_ref (connection->server->pending_conversations);
  handler->person_set =
    g_object_ref (connection->server->person_set);

  gml_request_handler_request_line_received (handler, method, query_string);

  connection->current_request_handler = handler;

  g_free (url_copy);

  return TRUE;
}

static gboolean
gml_server_header_received_cb (const char *field_name,
                               const char *value,
                               void *user_data)
{
  GmlServerConnection *connection = user_data;
  GmlRequestHandler *handler = connection->current_request_handler;

  gml_request_handler_header_received (handler, field_name, value);

  return TRUE;
}

static gboolean
gml_server_data_received_cb (const guint8 *data,
                             unsigned int length,
                             void *user_data)
{
  GmlServerConnection *connection = user_data;
  GmlRequestHandler *handler = connection->current_request_handler;

  gml_request_handler_data_received (handler, data, length);

  return TRUE;
}

static void
queue_response (GmlServerConnection *connection,
                GmlResponse *response)
{
  g_queue_push_tail (&connection->response_queue, response);
  g_signal_connect (response,
                    "changed",
                    G_CALLBACK (response_changed_cb),
                    connection);
}

static gboolean
gml_server_request_finished_cb (void *user_data)
{
  GmlServerConnection *connection = user_data;
  GmlRequestHandler *handler = connection->current_request_handler;
  GmlResponse *response;

  response = gml_request_handler_request_finished (handler);

  g_object_unref (handler);
  connection->current_request_handler = NULL;

  queue_response (connection, response);

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
gml_server_connection_pop_response (GmlServerConnection *connection)
{
  GmlResponse *response;
  int num_handlers;

  g_return_if_fail (!g_queue_is_empty (&connection->response_queue));

  response = g_queue_pop_head (&connection->response_queue);

  num_handlers =
    g_signal_handlers_disconnect_by_func (response,
                                          response_changed_cb,
                                          connection);
  g_warn_if_fail (num_handlers == 1);

  /* Whenever we end up with an empty response queue will start
   * counting the time the connection has been idle so that we can
   * remove it if it gets too old */
  if (g_queue_is_empty (&connection->response_queue))
    connection->no_response_age = gml_main_context_get_monotonic_clock (NULL);

  g_object_unref (response);
}

static void
gml_server_connection_clear_responses (GmlServerConnection *connection)
{
  while (!g_queue_is_empty (&connection->response_queue))
    gml_server_connection_pop_response (connection);

  if (connection->current_request_handler)
    {
      g_object_unref (connection->current_request_handler);
      connection->current_request_handler = NULL;
    }
}

static void
set_bad_input_with_code (GmlServerConnection *connection,
                         GmlStringResponseType code)
{
  GmlResponse *response;

  /* Replace all of the queued responses with an error response */
  gml_server_connection_clear_responses (connection);

  response = gml_string_response_new (code);

  queue_response (connection, response);

  connection->had_bad_input = TRUE;
}

static void
set_bad_input (GmlServerConnection *connection, GError *error)
{
  if (error->domain == GML_HTTP_PARSER_ERROR
      && error->code == GML_HTTP_PARSER_ERROR_UNSUPPORTED)
    set_bad_input_with_code (connection,
                             GML_STRING_RESPONSE_UNSUPPORTED_REQUEST);
  else
    set_bad_input_with_code (connection, GML_STRING_RESPONSE_BAD_REQUEST);
}

static void
check_dead_connection_cb (gpointer data,
                          gpointer user_data)
{
  GmlServerConnection *connection = data;

  if (g_queue_is_empty (&connection->response_queue)
      && ((gml_main_context_get_monotonic_clock (NULL)
           - connection->no_response_age)
          >= GML_SERVER_NO_RESPONSE_TIMEOUT))
    {
      /* If we've already had bad input then we'll just remove the
       * connection. This will happen if the client doesn't close its
       * end of the connection after we finish sending the bad input
       * message */
      if (connection->had_bad_input)
        gml_server_remove_connection (connection->server, connection);
      else
        {
          set_bad_input_with_code (connection,
                                   GML_STRING_RESPONSE_REQUEST_TIMEOUT);
          update_poll (connection);
        }
    }
}

static void
gml_server_run_gc (GmlServer *server)
{
  g_list_foreach (server->connections, check_dead_connection_cb, NULL);

  /* This is probably relatively expensive because it has to iterate
     the entire list of people, but it only happens infrequently so
     hopefully it's not a problem */
  gml_person_set_remove_useless_people (server->person_set);

  server->last_gc_time = gml_main_context_get_monotonic_clock (NULL);
}

static void
gml_server_remove_connection (GmlServer *server,
                              GmlServerConnection *connection)
{
  gml_server_connection_clear_responses (connection);

  gml_main_context_remove_source (connection->source);
  g_object_unref (connection->client_socket);
  server->connections = g_list_delete_link (server->connections,
                                            connection->list_node);
  g_free (connection->peer_address_string);
  g_slice_free (GmlServerConnection, connection);

  /* Reset the poll on the server socket in case we previously stopped
     listening because we ran out of file descriptors. This will do
     nothing if we were already listening */
  gml_main_context_modify_poll (server->server_socket_source,
                                GML_MAIN_CONTEXT_POLL_IN);
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
          gml_log ("shutdown socket failed for %s: %s",
                   connection->peer_address_string,
                   error->message);
          g_clear_error (&error);
          gml_server_remove_connection (connection->server, connection);
          return;
        }

      connection->write_finished = TRUE;
    }

  if (!connection->write_finished)
    {
      if (connection->output_length > 0)
        flags |= GML_MAIN_CONTEXT_POLL_OUT;
      else if (!g_queue_is_empty (&connection->response_queue))
        {
          GmlResponse *response
            = g_queue_peek_head (&connection->response_queue);

          if (gml_response_has_data (response))
            flags |= GML_MAIN_CONTEXT_POLL_OUT;
        }
    }

  /* If both ends of the connection are closed then we can abandon
     this connectin */
  if (connection->read_finished && connection->write_finished)
    gml_server_remove_connection (connection->server, connection);
  else
    gml_main_context_modify_poll (connection->source,
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

  if (flags & GML_MAIN_CONTEXT_POLL_ERROR)
    {
      int value;
      unsigned int value_len = sizeof (value);

      if (getsockopt (g_socket_get_fd (connection->client_socket),
                      SOL_SOCKET,
                      SO_ERROR,
                      &value,
                      &value_len) == -1
          || value_len != sizeof (value)
          || value == 0)
        gml_log ("Unknown error on socket for %s",
                 connection->peer_address_string);
      else
        gml_log ("Error on socket for %s: %s",
                 connection->peer_address_string,
                 strerror (value));

      gml_server_remove_connection (server, connection);
    }
  else if (flags & GML_MAIN_CONTEXT_POLL_IN)
    {
      GError *error = NULL;

      gssize got =
        g_socket_receive (connection->client_socket,
                          buf,
                          sizeof (buf),
                          NULL, /* cancellable */
                          &error);

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
              gml_log ("Error reading from socket for %s: %s",
                       connection->peer_address_string,
                       error->message);
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

          if (!gml_response_has_data (response))
            break;

          added =
            gml_response_add_data (response,
                                   connection->output_buffer
                                   + connection->output_length,
                                   GML_SERVER_OUTPUT_BUFFER_SIZE
                                   - connection->output_length);

          connection->output_length += added;

          /* If the response is now finished then remove it from the queue */
          if (gml_response_is_finished (response))
            gml_server_connection_pop_response (connection);
          /* If the buffer wasn't big enough to fit a chunk in then
             the response might not will the buffer so we should give
             up until the buffer is emptied */
          else
            break;
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
              g_print ("Error writing to socket for %s: %s",
                       connection->peer_address_string,
                       error->message);
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

static char *
get_peer_address_string (GSocket *client_socket)
{
  GSocketAddress *address = g_socket_get_remote_address (client_socket, NULL);
  char *address_string = NULL;

  if (address)
    {
      if (G_IS_INET_SOCKET_ADDRESS (address))
        {
          GInetSocketAddress *inet_socket_address
            = (GInetSocketAddress *) address;
          GInetAddress *inet_address
            = g_inet_socket_address_get_address (inet_socket_address);

          address_string =
            g_inet_address_to_string ((GInetAddress *) inet_address);
        }

      g_object_unref (address);
    }

  if (address_string == NULL)
    return g_strdup ("(unknown)");
  else
    return address_string;
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
      else if (error->domain == G_IO_ERROR
               && error->code == G_IO_ERROR_TOO_MANY_OPEN_FILES)
        {
          gml_log ("Too many open files to accept connection");

          /* Stop listening for new connections until someone disconnects */
          gml_main_context_modify_poll (server->server_socket_source,
                                        0);
          g_clear_error (&error);
        }
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
        gml_main_context_add_poll (NULL /* default context */,
                                   g_socket_get_fd (client_socket),
                                   GML_MAIN_CONTEXT_POLL_IN,
                                   gml_server_connection_poll_cb,
                                   connection);
      server->connections = g_list_prepend (server->connections,
                                            connection);

      gml_http_parser_init (&connection->http_parser,
                            &gml_server_http_parser_vtable,
                            connection);

      connection->current_request_handler = NULL;
      g_queue_init (&connection->response_queue);

      connection->had_bad_input = FALSE;
      connection->read_finished = FALSE;
      connection->write_finished = FALSE;

      connection->output_length = 0;

      /* If logging is available then we'll want to store the peer
         address as a string so we've got something to refer to */
      if (gml_log_available ())
        {
          connection->peer_address_string
            = get_peer_address_string (client_socket);
          gml_log ("Accepted connection from %s",
                   connection->peer_address_string);
        }
      else
        connection->peer_address_string = NULL;

      connection->no_response_age = gml_main_context_get_monotonic_clock (NULL);

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

  server->pending_conversations = gml_conversation_set_new ();

  server->server_socket_source =
    gml_main_context_add_poll (NULL /* default context */,
                               g_socket_get_fd (server->server_socket),
                               GML_MAIN_CONTEXT_POLL_IN,
                               gml_server_pending_connection_cb,
                               server);

  server->last_gc_time = gml_main_context_get_monotonic_clock (NULL);

  return server;

 error:
  if (server->server_socket)
    g_object_unref (server->server_socket);

  g_free (server);

  return NULL;
}

static void
gml_server_quit_cb (GmlMainContextSource *source,
                    void *user_data)
{
  gboolean *quit_received_ptr = user_data;

  *quit_received_ptr = TRUE;

  gml_log ("Quit signal received");
}

gboolean
gml_server_run (GmlServer *server,
                GError **error)
{
  GmlMainContextSource *quit_source;
  gboolean quit_received = FALSE;

  /* We have to make the quit source here instead of during
     gml_server_new because if we are daemonized then the process will
     be different by the time we reach here so the signalfd needs to
     be created in the new process */
  quit_source = gml_main_context_add_quit (NULL /* default context */,
                                           gml_server_quit_cb,
                                           &quit_received);

  while (TRUE)
    {
      gint64 wait_time;

      wait_time = (server->last_gc_time + GML_SERVER_GC_TIMEOUT
                   - gml_main_context_get_monotonic_clock (NULL));
      if (wait_time < 0)
        wait_time = 0;

      gml_main_context_poll (NULL /* default context */,
                             /* microseconds to milliseconds rounding up */
                             (wait_time + 999) / 1000);

      if (quit_received || server->fatal_error)
        break;

      if (gml_main_context_get_monotonic_clock (NULL)
          - server->last_gc_time
          >= GML_SERVER_GC_TIMEOUT)
        gml_server_run_gc (server);
    }

  gml_main_context_remove_source (quit_source);

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

  g_object_unref (server->person_set);

  g_object_unref (server->pending_conversations);

  gml_main_context_remove_source (server->server_socket_source);

  g_object_unref (server->server_socket);

  g_free (server);
}
