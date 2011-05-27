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
};

static void
gml_server_quit_cb (GmlMainContextSource *source,
                    void *user_data)
{
  GmlServer *server = user_data;

  server->quit_received = TRUE;
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
      GSocketAddress *address =
        g_socket_get_remote_address (client_socket,
                                     &error);

      if (address == NULL)
        g_print ("Error getting remote address: %s\n", error->message);
      else
        {
          GInetAddress *inet_address;
          char *name;
          guint16 port;

          g_assert (G_IS_INET_SOCKET_ADDRESS (address));

          inet_address =
            g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (address));

          name = g_inet_address_to_string (inet_address);

          port =
            g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (address));

          g_print ("Got connection from: %s:%i\n",
                   name, (int) port);

          g_object_unref (address);

          g_free (name);
        }

      g_object_unref (client_socket);
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
  gml_main_context_remove_source (server->main_context,
                                  server->quit_source);

  gml_main_context_remove_source (server->main_context,
                                  server->server_socket_source);
  gml_main_context_free (server->main_context);

  g_object_unref (server->server_socket);

  g_free (server);
}
