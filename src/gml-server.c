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

#include "gml-server.h"
#include "gml-main-context.h"
#include "gml-http-parser.h"

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
};

typedef struct
{
  GmlServer *server;

  GSocket *client_socket;
  GmlMainContextSource *source;

  /* Pointer to the GList node in the connections list so it can be
     removed quickly */
  GList *list_node;

  GmlHttpParser http_parser;
} GmlServerConnection;

static gboolean
gml_server_request_line_received_cb (const char *method,
                                     const char *uri,
                                     void *user_data)
{
  g_print ("request line received \"%s\" \"%s\"\n",
           method,
           uri);

  return TRUE;
}

static gboolean
gml_server_header_received_cb (const char *field_name,
                               const char *value,
                               void *user_data)
{
  g_print ("header received \"%s\" \"%s\"\n",
           field_name,
           value);

  return TRUE;
}

static gboolean
gml_server_data_received_cb (const guint8 *data,
                             unsigned int length,
                             void *user_data)
{
  g_print ("data received \"%.*s\"\n",
           length, data);

  return TRUE;
}

static gboolean
gml_server_request_finished_cb (void *user_data)
{
  g_print ("request finished\n");

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
gml_server_remove_connection (GmlServer *server,
                              GmlServerConnection *connection)
{
  gml_main_context_remove_source (server->main_context, connection->source);
  g_object_unref (connection->client_socket);
  server->connections = g_list_delete_link (server->connections,
                                            connection->list_node);
  g_slice_free (GmlServerConnection, connection);
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
          g_print ("EOF from socket\n");
          gml_server_remove_connection (server, connection);
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
      else if (!gml_http_parser_parse_data (&connection->http_parser,
                                            (guint8 *) buf,
                                            got,
                                            &error))
        {
          g_print ("%s\n", error->message);
          g_clear_error (&error);
          gml_server_remove_connection (server, connection);
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

  gml_main_context_remove_source (server->main_context,
                                  server->quit_source);

  gml_main_context_remove_source (server->main_context,
                                  server->server_socket_source);
  gml_main_context_free (server->main_context);

  g_object_unref (server->server_socket);

  g_free (server);
}
