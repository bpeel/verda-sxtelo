/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
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

#include "vsx-server.h"
#include "vsx-main-context.h"
#include "vsx-http-parser.h"
#include "vsx-person-set.h"
#include "vsx-string-response.h"
#include "vsx-conversation.h"
#include "vsx-conversation-set.h"
#include "vsx-new-person-handler.h"
#include "vsx-leave-handler.h"
#include "vsx-send-message-handler.h"
#include "vsx-watch-person-handler.h"
#include "vsx-start-typing-handler.h"
#include "vsx-stop-typing-handler.h"
#include "vsx-keep-alive-handler.h"
#include "vsx-log.h"

struct _VsxServer
{
  VsxMainContextSource *server_socket_source;
  GSocket *server_socket;

  /* If this gets set then vsx_server_run will return and report the
     error */
  GError *fatal_error;

  /* List of open connections */
  GList *connections;

  VsxConversationSet *pending_conversations;

  VsxPersonSet *person_set;

  gint64 last_gc_time;
};

#define VSX_SERVER_OUTPUT_BUFFER_SIZE 1024

typedef struct
{
  VsxServer *server;

  GSocket *client_socket;
  VsxMainContextSource *source;

  /* Pointer to the GList node in the connections list so it can be
     removed quickly */
  GList *list_node;

  VsxHttpParser http_parser;

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
  VsxRequestHandler *current_request_handler;

  /* Queue of VsxResponses to send to this client */
  GQueue response_queue;

  unsigned int output_length;
  guint8 output_buffer[VSX_SERVER_OUTPUT_BUFFER_SIZE];

  /* IP address of the connection. This is only filled in if logging
     is enabled */
  char *peer_address_string;

  /* Time since the response queue became empty. The connection will
   * be removed if this stays empty for too long */
  gint64 no_response_age;
} VsxServerConnection;

/* Interval time in micro-seconds to run the dead person garbage
   collector */
#define VSX_SERVER_GC_TIMEOUT (5 * 60 * (gint64) 1000000)

/* Time in microseconds after which a connection with no responses
 * will be considered dead. This is necessary to avoid keeping around
 * connections that open the socket and then don't send any
 * data. These would otherwise hang out indefinitely and use up
 * resources. */
#define VSX_SERVER_NO_RESPONSE_TIMEOUT (5 * 60 * (gint64) 1000000)

static const struct
{
  const char *url;
  GType (* get_type_func) (void);
}
requests[] =
  {
    { "/keep_alive", vsx_keep_alive_handler_get_type },
    { "/start_typing", vsx_start_typing_handler_get_type },
    { "/stop_typing", vsx_stop_typing_handler_get_type },
    { "/send_message", vsx_send_message_handler_get_type },
    { "/watch_person", vsx_watch_person_handler_get_type },
    { "/new_person", vsx_new_person_handler_get_type },
    { "/leave", vsx_leave_handler_get_type }
  };

static void
update_poll (VsxServerConnection *connection);

static void
vsx_server_remove_connection (VsxServer *server,
                              VsxServerConnection *connection);

static void
response_changed_cb (VsxResponse *response,
                     VsxServerConnection *connection)
{
  if (response == g_queue_peek_head (&connection->response_queue))
    update_poll (connection);
}

static gboolean
vsx_server_request_line_received_cb (const char *method_str,
                                     const char *uri,
                                     void *user_data)
{
  VsxServerConnection *connection = user_data;
  const char *query_string;
  const char *question_mark;
  const char *url;
  VsxRequestHandler *handler;
  VsxRequestMethod method;
  char *url_copy = NULL;
  int i;

  g_warn_if_fail (connection->current_request_handler == NULL);

  if (!strcmp (method_str, "GET"))
    method = VSX_REQUEST_METHOD_GET;
  else if (!strcmp (method_str, "POST"))
    method = VSX_REQUEST_METHOD_POST;
  else if (!strcmp (method_str, "OPTIONS"))
    method = VSX_REQUEST_METHOD_OPTIONS;
  else
    method = VSX_REQUEST_METHOD_UNKNOWN;

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
  handler = vsx_request_handler_new ();

 got_handler:
  handler->socket_address =
    g_socket_get_remote_address (connection->client_socket, NULL);
  handler->conversation_set =
    g_object_ref (connection->server->pending_conversations);
  handler->person_set =
    g_object_ref (connection->server->person_set);

  vsx_request_handler_request_line_received (handler, method, query_string);

  connection->current_request_handler = handler;

  g_free (url_copy);

  return TRUE;
}

static gboolean
vsx_server_header_received_cb (const char *field_name,
                               const char *value,
                               void *user_data)
{
  VsxServerConnection *connection = user_data;
  VsxRequestHandler *handler = connection->current_request_handler;

  vsx_request_handler_header_received (handler, field_name, value);

  return TRUE;
}

static gboolean
vsx_server_data_received_cb (const guint8 *data,
                             unsigned int length,
                             void *user_data)
{
  VsxServerConnection *connection = user_data;
  VsxRequestHandler *handler = connection->current_request_handler;

  vsx_request_handler_data_received (handler, data, length);

  return TRUE;
}

static void
queue_response (VsxServerConnection *connection,
                VsxResponse *response)
{
  g_queue_push_tail (&connection->response_queue, response);
  g_signal_connect (response,
                    "changed",
                    G_CALLBACK (response_changed_cb),
                    connection);
}

static gboolean
vsx_server_request_finished_cb (void *user_data)
{
  VsxServerConnection *connection = user_data;
  VsxRequestHandler *handler = connection->current_request_handler;
  VsxResponse *response;

  response = vsx_request_handler_request_finished (handler);

  g_object_unref (handler);
  connection->current_request_handler = NULL;

  queue_response (connection, response);

  return TRUE;
}

static VsxHttpParserVtable
vsx_server_http_parser_vtable =
  {
    .request_line_received = vsx_server_request_line_received_cb,
    .header_received = vsx_server_header_received_cb,
    .data_received = vsx_server_data_received_cb,
    .request_finished = vsx_server_request_finished_cb
  };

static void
vsx_server_connection_pop_response (VsxServerConnection *connection)
{
  VsxResponse *response;
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
    connection->no_response_age = vsx_main_context_get_monotonic_clock (NULL);

  g_object_unref (response);
}

static void
vsx_server_connection_clear_responses (VsxServerConnection *connection)
{
  while (!g_queue_is_empty (&connection->response_queue))
    vsx_server_connection_pop_response (connection);

  if (connection->current_request_handler)
    {
      g_object_unref (connection->current_request_handler);
      connection->current_request_handler = NULL;
    }
}

static void
set_bad_input_with_code (VsxServerConnection *connection,
                         VsxStringResponseType code)
{
  VsxResponse *response;

  /* Replace all of the queued responses with an error response */
  vsx_server_connection_clear_responses (connection);

  response = vsx_string_response_new (code);

  queue_response (connection, response);

  connection->had_bad_input = TRUE;
}

static void
set_bad_input (VsxServerConnection *connection, GError *error)
{
  if (error->domain == VSX_HTTP_PARSER_ERROR
      && error->code == VSX_HTTP_PARSER_ERROR_UNSUPPORTED)
    set_bad_input_with_code (connection,
                             VSX_STRING_RESPONSE_UNSUPPORTED_REQUEST);
  else
    set_bad_input_with_code (connection, VSX_STRING_RESPONSE_BAD_REQUEST);
}

static void
check_dead_connection_cb (gpointer data,
                          gpointer user_data)
{
  VsxServerConnection *connection = data;

  if (g_queue_is_empty (&connection->response_queue)
      && ((vsx_main_context_get_monotonic_clock (NULL)
           - connection->no_response_age)
          >= VSX_SERVER_NO_RESPONSE_TIMEOUT))
    {
      /* If we've already had bad input then we'll just remove the
       * connection. This will happen if the client doesn't close its
       * end of the connection after we finish sending the bad input
       * message */
      if (connection->had_bad_input)
        vsx_server_remove_connection (connection->server, connection);
      else
        {
          set_bad_input_with_code (connection,
                                   VSX_STRING_RESPONSE_REQUEST_TIMEOUT);
          update_poll (connection);
        }
    }
}

static void
vsx_server_run_gc (VsxServer *server)
{
  g_list_foreach (server->connections, check_dead_connection_cb, NULL);

  /* This is probably relatively expensive because it has to iterate
     the entire list of people, but it only happens infrequently so
     hopefully it's not a problem */
  vsx_person_set_remove_silent_people (server->person_set);

  server->last_gc_time = vsx_main_context_get_monotonic_clock (NULL);
}

static void
vsx_server_remove_connection (VsxServer *server,
                              VsxServerConnection *connection)
{
  vsx_server_connection_clear_responses (connection);

  vsx_main_context_remove_source (connection->source);
  g_object_unref (connection->client_socket);
  server->connections = g_list_delete_link (server->connections,
                                            connection->list_node);
  g_free (connection->peer_address_string);
  g_slice_free (VsxServerConnection, connection);

  /* Reset the poll on the server socket in case we previously stopped
     listening because we ran out of file descriptors. This will do
     nothing if we were already listening */
  vsx_main_context_modify_poll (server->server_socket_source,
                                VSX_MAIN_CONTEXT_POLL_IN);
}

static void
update_poll (VsxServerConnection *connection)
{
  VsxMainContextPollFlags flags = 0;

  if (!connection->read_finished)
    flags |= VSX_MAIN_CONTEXT_POLL_IN;

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
          vsx_log ("shutdown socket failed for %s: %s",
                   connection->peer_address_string,
                   error->message);
          g_clear_error (&error);
          vsx_server_remove_connection (connection->server, connection);
          return;
        }

      connection->write_finished = TRUE;
    }

  if (!connection->write_finished)
    {
      if (connection->output_length > 0)
        flags |= VSX_MAIN_CONTEXT_POLL_OUT;
      else if (!g_queue_is_empty (&connection->response_queue))
        {
          VsxResponse *response
            = g_queue_peek_head (&connection->response_queue);

          if (vsx_response_has_data (response))
            flags |= VSX_MAIN_CONTEXT_POLL_OUT;
        }
    }

  /* If both ends of the connection are closed then we can abandon
     this connectin */
  if (connection->read_finished && connection->write_finished)
    vsx_server_remove_connection (connection->server, connection);
  else
    vsx_main_context_modify_poll (connection->source,
                                  flags);
}

static void
vsx_server_connection_poll_cb (VsxMainContextSource *source,
                               int fd,
                               VsxMainContextPollFlags flags,
                               void *user_data)
{
  VsxServerConnection *connection = user_data;
  VsxServer *server = connection->server;
  char buf[1024];

  if (flags & VSX_MAIN_CONTEXT_POLL_ERROR)
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
        vsx_log ("Unknown error on socket for %s",
                 connection->peer_address_string);
      else
        vsx_log ("Error on socket for %s: %s",
                 connection->peer_address_string,
                 strerror (value));

      vsx_server_remove_connection (server, connection);
    }
  else if (flags & VSX_MAIN_CONTEXT_POLL_IN)
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
              && !vsx_http_parser_parser_eof (&connection->http_parser,
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
              vsx_log ("Error reading from socket for %s: %s",
                       connection->peer_address_string,
                       error->message);
              vsx_server_remove_connection (server, connection);
            }

          g_clear_error (&error);
        }
      else
        {
          if (!connection->had_bad_input
              && !vsx_http_parser_parse_data (&connection->http_parser,
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
  else if (flags & VSX_MAIN_CONTEXT_POLL_OUT)
    {
      GError *error = NULL;
      gssize wrote;

      /* Try to fill the output buffer as much as possible before
         initiating a write */
      while (connection->output_length < VSX_SERVER_OUTPUT_BUFFER_SIZE
             && !g_queue_is_empty (&connection->response_queue))
        {
          VsxResponse *response =
            g_queue_peek_head (&connection->response_queue);
          unsigned int added;

          if (!vsx_response_has_data (response))
            break;

          added =
            vsx_response_add_data (response,
                                   connection->output_buffer
                                   + connection->output_length,
                                   VSX_SERVER_OUTPUT_BUFFER_SIZE
                                   - connection->output_length);

          connection->output_length += added;

          /* If the response is now finished then remove it from the queue */
          if (vsx_response_is_finished (response))
            vsx_server_connection_pop_response (connection);
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
              vsx_server_remove_connection (server, connection);
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
vsx_server_pending_connection_cb (VsxMainContextSource *source,
                                  int fd,
                                  VsxMainContextPollFlags flags,
                                  void *user_data)
{
  VsxServer *server = user_data;
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
          vsx_log ("Too many open files to accept connection");

          /* Stop listening for new connections until someone disconnects */
          vsx_main_context_modify_poll (server->server_socket_source,
                                        0);
          g_clear_error (&error);
        }
      else
        /* This will cause vsx_server_run to return */
        server->fatal_error = error;
    }
  else
    {
      VsxServerConnection *connection;

      g_socket_set_blocking (client_socket, FALSE);

      connection = g_slice_new (VsxServerConnection);

      connection->server = server;
      connection->client_socket = client_socket;
      connection->source =
        vsx_main_context_add_poll (NULL /* default context */,
                                   g_socket_get_fd (client_socket),
                                   VSX_MAIN_CONTEXT_POLL_IN,
                                   vsx_server_connection_poll_cb,
                                   connection);
      server->connections = g_list_prepend (server->connections,
                                            connection);

      vsx_http_parser_init (&connection->http_parser,
                            &vsx_server_http_parser_vtable,
                            connection);

      connection->current_request_handler = NULL;
      g_queue_init (&connection->response_queue);

      connection->had_bad_input = FALSE;
      connection->read_finished = FALSE;
      connection->write_finished = FALSE;

      connection->output_length = 0;

      /* If logging is available then we'll want to store the peer
         address as a string so we've got something to refer to */
      if (vsx_log_available ())
        {
          connection->peer_address_string
            = get_peer_address_string (client_socket);
          vsx_log ("Accepted connection from %s",
                   connection->peer_address_string);
        }
      else
        connection->peer_address_string = NULL;

      connection->no_response_age = vsx_main_context_get_monotonic_clock (NULL);

      /* Store the list node so we can quickly remove the connection
         from the list */
      connection->list_node = server->connections;
    }
}

VsxServer *
vsx_server_new (GSocketAddress *address,
                GError **error)
{
  VsxServer *server;

  g_return_val_if_fail (G_IS_SOCKET_ADDRESS (address), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  server = g_new0 (VsxServer, 1);

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

  server->person_set = vsx_person_set_new ();

  server->pending_conversations = vsx_conversation_set_new ();

  server->server_socket_source =
    vsx_main_context_add_poll (NULL /* default context */,
                               g_socket_get_fd (server->server_socket),
                               VSX_MAIN_CONTEXT_POLL_IN,
                               vsx_server_pending_connection_cb,
                               server);

  server->last_gc_time = vsx_main_context_get_monotonic_clock (NULL);

  return server;

 error:
  if (server->server_socket)
    g_object_unref (server->server_socket);

  g_free (server);

  return NULL;
}

static void
vsx_server_quit_cb (VsxMainContextSource *source,
                    void *user_data)
{
  gboolean *quit_received_ptr = user_data;

  *quit_received_ptr = TRUE;

  vsx_log ("Quit signal received");
}

gboolean
vsx_server_run (VsxServer *server,
                GError **error)
{
  VsxMainContextSource *quit_source;
  gboolean quit_received = FALSE;

  /* We have to make the quit source here instead of during
     vsx_server_new because if we are daemonized then the process will
     be different by the time we reach here so the signalfd needs to
     be created in the new process */
  quit_source = vsx_main_context_add_quit (NULL /* default context */,
                                           vsx_server_quit_cb,
                                           &quit_received);

  while (TRUE)
    {
      gint64 wait_time;

      wait_time = (server->last_gc_time + VSX_SERVER_GC_TIMEOUT
                   - vsx_main_context_get_monotonic_clock (NULL));
      if (wait_time < 0)
        wait_time = 0;

      vsx_main_context_poll (NULL /* default context */,
                             /* microseconds to milliseconds rounding up */
                             (wait_time + 999) / 1000);

      if (quit_received || server->fatal_error)
        break;

      if (vsx_main_context_get_monotonic_clock (NULL)
          - server->last_gc_time
          >= VSX_SERVER_GC_TIMEOUT)
        vsx_server_run_gc (server);
    }

  vsx_main_context_remove_source (quit_source);

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
vsx_server_free (VsxServer *server)
{
  while (server->connections)
    vsx_server_remove_connection (server, server->connections->data);

  g_object_unref (server->person_set);

  g_object_unref (server->pending_conversations);

  vsx_main_context_remove_source (server->server_socket_source);

  g_object_unref (server->server_socket);

  g_free (server);
}
