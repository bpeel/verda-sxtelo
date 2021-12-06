/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2011, 2013  Neil Roberts
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
#include <openssl/ssl.h>

#include "vsx-server.h"
#include "vsx-main-context.h"
#include "vsx-person-set.h"
#include "vsx-connection.h"
#include "vsx-conversation.h"
#include "vsx-conversation-set.h"
#include "vsx-log.h"
#include "vsx-ssl-error.h"
#include "vsx-proto.h"
#include "vsx-util.h"

#define DEFAULT_PORT 5144
#define DEFAULT_SSL_PORT (DEFAULT_PORT + 1)

struct _VsxServer
{
  /* List of VsxServerSockets */
  VsxList sockets;

  /* If this gets set then vsx_server_run will return and report the
     error */
  GError *fatal_error;

  /* List of open connections */
  VsxList connections;

  VsxConversationSet *pending_conversations;

  VsxPersonSet *person_set;

  VsxMainContextSource *gc_source;
};

/* Make sure the output buffer is large enough to contain the largest
 * payload plus the corresponding frame header.
 */
#define VSX_SERVER_OUTPUT_BUFFER_SIZE (1 + 1 + 2 + VSX_PROTO_MAX_PAYLOAD_SIZE)

typedef struct
{
  VsxServer *server;

  GSocket *client_socket;
  VsxMainContextSource *source;

  /* List node within the list of connections */
  VsxList link;

  VsxConnection *ws_connection;
  VsxListener ws_connection_listener;

  /* This becomes true when we've received something from the client
     that we don't understand and we're ignoring any further input */
  bool had_bad_input;
  /* This becomes true when the client has closed its end of the
     connection */
  bool read_finished;
  /* This becomes true when we've stopped writing data. This will only
     happen after the client closes its connection or we've had bad
     input and we're ignoring further data */
  bool write_finished;

  /* If we’ve already started an SSL_read that needed to block in
   * order to continue, these are the flags needed to complete it. */
  VsxMainContextPollFlags ssl_read_block;
  /* Same for an SSL_write */
  VsxMainContextPollFlags ssl_write_block;

  unsigned int output_length;
  uint8_t output_buffer[VSX_SERVER_OUTPUT_BUFFER_SIZE];

  /* IP address of the connection. This is only filled in if logging
     is enabled */
  char *peer_address_string;

  /* Time since the response queue became empty. The connection will
   * be removed if this stays empty for too long */
  int64_t no_response_age;

  SSL *ssl;
} VsxServerConnection;

typedef struct
{
  VsxList link;
  VsxMainContextSource *source;
  GSocket *socket;
  VsxServer *server;
  SSL_CTX *ssl_ctx;
} VsxServerSocket;

/* Interval time in minutes to run the dead person garbage
   collector */
#define VSX_SERVER_GC_TIMEOUT 5

/* Time in microseconds after which a connection with no responses
 * will be considered dead. This is necessary to avoid keeping around
 * connections that open the socket and then don't send any
 * data. These would otherwise hang out indefinitely and use up
 * resources. */
#define VSX_SERVER_NO_RESPONSE_TIMEOUT (5 * 60 * (int64_t) 1000000)

static void
update_poll (VsxServerConnection *connection);

static void
vsx_server_remove_connection (VsxServer *server,
                              VsxServerConnection *connection);

static void
ws_connection_changed_cb (VsxListener *listener,
                          void *data)
{
  VsxServerConnection *connection =
    vsx_container_of (listener, connection, ws_connection_listener);

  update_poll (connection);
}

static void
set_bad_input (VsxServerConnection *connection)
{
  connection->had_bad_input = true;
}

static void
set_bad_input_with_error (VsxServerConnection *connection, GError *error)
{
  vsx_log ("For %s: %s",
           connection->peer_address_string,
           error->message);

  set_bad_input (connection);
}

static void
check_dead_connection (VsxServerConnection *connection)
{
  if (vsx_main_context_get_monotonic_clock (NULL)
      - vsx_connection_get_last_message_time (connection->ws_connection)
      >= VSX_SERVER_NO_RESPONSE_TIMEOUT)
    {
      /* If we've already had bad input then we'll just remove the
       * connection. This will happen if the client doesn't close its
       * end of the connection after we finish sending the bad input
       * message */
      if (connection->had_bad_input)
        vsx_server_remove_connection (connection->server, connection);
      else
        {
          set_bad_input (connection);
          update_poll (connection);
        }
    }
}

static void
vsx_server_gc_cb (VsxMainContextSource *source,
                  void *user_data)
{
  VsxServer *server = user_data;
  VsxServerConnection *connection, *tmp;

  vsx_list_for_each_safe (connection, tmp, &server->connections, link)
    check_dead_connection (connection);
}

static void
vsx_server_remove_connection (VsxServer *server,
                              VsxServerConnection *connection)
{
  if (connection->ssl)
    SSL_free(connection->ssl);

  vsx_main_context_remove_source (connection->source);
  g_object_unref (connection->client_socket);
  vsx_list_remove (&connection->link);
  g_free (connection->peer_address_string);

  vsx_connection_free (connection->ws_connection);

  vsx_free (connection);

  if (vsx_list_empty (&server->connections))
    {
      vsx_main_context_remove_source (server->gc_source);
      server->gc_source = NULL;
    }

  /* Reset the poll on the server sockets in case we previously
     stopped listening because we ran out of file descriptors. This
     will do nothing if we were already listening */
  VsxServerSocket *ssocket;

  vsx_list_for_each (ssocket, &server->sockets, link)
    {
      vsx_main_context_modify_poll (ssocket->source, VSX_MAIN_CONTEXT_POLL_IN);
    }
}

static void
vsx_server_remove_socket (VsxServer *server,
                          VsxServerSocket *ssocket)
{
  if (ssocket->ssl_ctx)
    SSL_CTX_free (ssocket->ssl_ctx);

  if (ssocket->source)
    vsx_main_context_remove_source (ssocket->source);

  if (ssocket->socket)
    g_object_unref (ssocket->socket);

  vsx_list_remove (&ssocket->link);

  g_free (ssocket);
}

static void
log_ssl_error (VsxServerConnection *connection)
{
  GError *error = NULL;

  vsx_ssl_error_set (&error);
  vsx_log ("For %s: %s", connection->peer_address_string, error->message);
  g_clear_error (&error);
}

static void
update_poll (VsxServerConnection *connection)
{
  VsxMainContextPollFlags flags = 0;

  if (connection->ssl_read_block)
    flags |= connection->ssl_read_block;
  else if (!connection->read_finished)
    flags |= VSX_MAIN_CONTEXT_POLL_IN;

  /* Shutdown the socket if we've finished writing */
  if (!connection->write_finished
      && connection->output_length == 0
      && (connection->had_bad_input
          || (connection->read_finished
              && vsx_connection_is_finished (connection->ws_connection))))
    {
      if (connection->ssl)
        {
          int ret = SSL_shutdown (connection->ssl);

          if (ret >= 0)
            {
              connection->write_finished = true;
            }
          else
            {
              switch (SSL_get_error (connection->ssl, ret))
                {
                case SSL_ERROR_WANT_READ:
                  flags |= VSX_MAIN_CONTEXT_POLL_IN;
                  break;
                case SSL_ERROR_WANT_WRITE:
                  flags |= VSX_MAIN_CONTEXT_POLL_OUT;
                  break;
                default:
                  log_ssl_error (connection);
                  vsx_server_remove_connection (connection->server, connection);
                  return;
                }
            }
        }
      else
        {
          GError *error = NULL;

          if (!g_socket_shutdown (connection->client_socket,
                                  false, /* shutdown_read */
                                  true, /* shutdown_write */
                                  &error))
            {
              vsx_log ("shutdown socket failed for %s: %s",
                       connection->peer_address_string,
                       error->message);
              g_clear_error (&error);
              vsx_server_remove_connection (connection->server, connection);
              return;
            }

          connection->write_finished = true;
        }
    }

  if (!connection->write_finished)
    {
      if (connection->ssl_write_block)
        flags |= connection->ssl_write_block;
      else if (connection->output_length > 0)
        flags |= VSX_MAIN_CONTEXT_POLL_OUT;
      else if (vsx_connection_has_data (connection->ws_connection))
        flags |= VSX_MAIN_CONTEXT_POLL_OUT;
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
handle_read (VsxServer *server,
             VsxServerConnection *connection)
{
  GError *error = NULL;

  if (connection->read_finished)
    {
      /* This might happen if the SSL_Shutdown command triggered a
       * poll for input */
      update_poll (connection);
      return;
    }

  char buf[1024];

  gssize got;

  if (connection->ssl)
    {
      connection->ssl_read_block = 0;

      got = SSL_read (connection->ssl, buf, sizeof (buf));

      if (got <= 0)
        {
          switch (SSL_get_error (connection->ssl, got))
            {
            case SSL_ERROR_ZERO_RETURN:
              got = 0;
              break;
            case SSL_ERROR_WANT_READ:
              connection->ssl_read_block = VSX_MAIN_CONTEXT_POLL_IN;
              update_poll (connection);
              return;
            case SSL_ERROR_WANT_WRITE:
              connection->ssl_read_block = VSX_MAIN_CONTEXT_POLL_OUT;
              update_poll (connection);
              return;
            default:
              log_ssl_error (connection);
              vsx_server_remove_connection (connection->server, connection);
              return;
            }
        }
    }
  else
    {
      got = g_socket_receive (connection->client_socket,
                              buf,
                              sizeof (buf),
                              NULL, /* cancellable */
                              &error);
      if (got == -1)
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
          return;
        }
    }

  if (got == 0)
    {
      if (!connection->had_bad_input)
        {
          if (!vsx_connection_parse_eof (connection->ws_connection, &error))
            {
              set_bad_input_with_error (connection, error);
              g_clear_error (&error);
            }
        }

      connection->read_finished = true;

      update_poll (connection);
    }
  else
    {
      if (!connection->had_bad_input
          && !vsx_connection_parse_data (connection->ws_connection,
                                         (uint8_t *) buf,
                                         got,
                                         &error))
        {
          set_bad_input_with_error (connection, error);
          g_clear_error (&error);
        }

      update_poll (connection);
    }
}

static void
fill_output_buffer (VsxServerConnection *connection)
{
  size_t added =
    vsx_connection_fill_output_buffer (connection->ws_connection,
                                       connection->output_buffer
                                       + connection->output_length,
                                       VSX_SERVER_OUTPUT_BUFFER_SIZE
                                       - connection->output_length);

  connection->output_length += added;
}

static void
handle_write (VsxServer *server,
              VsxServerConnection *connection)
{
  GError *error = NULL;
  gssize wrote;

  if (connection->ssl_write_block == 0)
    fill_output_buffer (connection);

  if (connection->output_length == 0)
    {
      /* This might happen if the SSL_Shutdown command triggered a
       * poll for output */
      update_poll (connection);
      return;
    }


  if (connection->ssl)
    {
      connection->ssl_write_block = 0;

      wrote = SSL_write (connection->ssl,
                         connection->output_buffer,
                         connection->output_length);

      if (wrote <= 0)
        {
          switch (SSL_get_error (connection->ssl, wrote))
            {
            case SSL_ERROR_WANT_READ:
              connection->ssl_write_block = VSX_MAIN_CONTEXT_POLL_IN;
              update_poll (connection);
              return;
            case SSL_ERROR_WANT_WRITE:
              connection->ssl_write_block = VSX_MAIN_CONTEXT_POLL_OUT;
              update_poll (connection);
              return;
            default:
              log_ssl_error (connection);
              vsx_server_remove_connection (connection->server, connection);
              return;
            }
        }
    }
  else
    {
      wrote = g_socket_send (connection->client_socket,
                             (const char *) connection->output_buffer,
                             connection->output_length,
                             NULL,
                             &error);

      if (wrote == -1)
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
          return;
        }
    }

  /* Move any remaining data in the output buffer to the front */
  memmove (connection->output_buffer,
           connection->output_buffer + wrote,
           connection->output_length - wrote);
  connection->output_length -= wrote;

  update_poll (connection);
}

static void
vsx_server_connection_poll_cb (VsxMainContextSource *source,
                               int fd,
                               VsxMainContextPollFlags flags,
                               void *user_data)
{
  VsxServerConnection *connection = user_data;
  VsxServer *server = connection->server;

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
  else if (connection->ssl_read_block
           && ((flags & connection->ssl_read_block)
               == connection->ssl_read_block))
    {
      handle_read (server, connection);
    }
  else if (connection->ssl_write_block
           && ((flags & connection->ssl_write_block)
               == connection->ssl_write_block))
    {
      handle_write (server, connection);
    }
  else if (flags & VSX_MAIN_CONTEXT_POLL_IN)
    {
      handle_read (server, connection);
    }
  else if (flags & VSX_MAIN_CONTEXT_POLL_OUT)
    {
      handle_write (server, connection);
    }
}

static char *
get_address_string (GSocketAddress *address,
                    bool include_port)
{
  char *address_string = NULL;

  if (address)
    {
      if (G_IS_INET_SOCKET_ADDRESS (address))
        {
          GInetSocketAddress *inet_socket_address =
            (GInetSocketAddress *) address;
          GInetAddress *inet_address =
            g_inet_socket_address_get_address (inet_socket_address);
          char *host_part = g_inet_address_to_string (inet_address);

          if (include_port)
            {
              uint16_t port =
                g_inet_socket_address_get_port (inet_socket_address);

              address_string = g_strdup_printf ("%s:%u", host_part, port);
              g_free (host_part);
            }
          else
            address_string = host_part;
        }

      g_object_unref (address);
    }

  if (address_string == NULL)
    return g_strdup ("(unknown)");
  else
    return address_string;
}

static char *
get_peer_address_string (GSocket *client_socket)
{
  GSocketAddress *address = g_socket_get_remote_address (client_socket, NULL);

  return get_address_string (address, false /* include_port */);
}

static bool
init_connection_ssl (VsxServerConnection *connection,
                     SSL_CTX *ssl_ctx,
                     GError **error)
{
  connection->ssl = SSL_new (ssl_ctx);

  if (connection->ssl == NULL)
    goto error;

  SSL_set_accept_state (connection->ssl);

  if (!SSL_set_fd (connection->ssl,
                   g_socket_get_fd (connection->client_socket)))
    goto error;

  return true;

 error:
  vsx_ssl_error_set (error);
  return false;
}

static void
vsx_server_pending_connection_cb (VsxMainContextSource *source,
                                  int fd,
                                  VsxMainContextPollFlags flags,
                                  void *user_data)
{
  VsxServerSocket *ssocket = user_data;
  VsxServer *server = ssocket->server;
  GSocket *client_socket;
  GError *error = NULL;

  client_socket = g_socket_accept (ssocket->socket, NULL, &error);

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
          vsx_main_context_modify_poll (source, 0);
          g_clear_error (&error);
        }
      else
        /* This will cause vsx_server_run to return */
        server->fatal_error = error;
    }
  else
    {
      g_socket_set_blocking (client_socket, false);

      VsxServerConnection *connection = vsx_alloc (sizeof *connection);

      connection->server = server;
      connection->client_socket = client_socket;
      connection->source =
        vsx_main_context_add_poll (NULL /* default context */,
                                   g_socket_get_fd (client_socket),
                                   VSX_MAIN_CONTEXT_POLL_IN,
                                   vsx_server_connection_poll_cb,
                                   connection);
      vsx_list_insert (&server->connections, &connection->link);

      GSocketAddress *remote_address =
        g_socket_get_remote_address (client_socket, NULL);
      connection->ws_connection =
        vsx_connection_new (remote_address,
                            server->pending_conversations,
                            server->person_set);
      VsxSignal *changed_signal =
        vsx_connection_get_changed_signal (connection->ws_connection);
      connection->ws_connection_listener.notify =
        ws_connection_changed_cb;
      vsx_signal_add (changed_signal,
                      &connection->ws_connection_listener);
      g_object_unref (remote_address);

      connection->had_bad_input = false;
      connection->read_finished = false;
      connection->write_finished = false;
      connection->ssl_read_block = 0;
      connection->ssl_write_block = 0;
      connection->ssl = NULL;

      connection->output_length = 0;

      /* If logging is available then we'll want to store the peer
         address as a string so we've got something to refer to */
      if (vsx_log_available ())
        {
          connection->peer_address_string
            = get_peer_address_string (client_socket);
          vsx_log ("Accepted WebSocket%s connection from %s",
                   ssocket->ssl_ctx ? " SSL" : "",
                   connection->peer_address_string);
        }
      else
        connection->peer_address_string = NULL;

      connection->no_response_age = vsx_main_context_get_monotonic_clock (NULL);

      if (ssocket->ssl_ctx
          && !init_connection_ssl (connection, ssocket->ssl_ctx, &error))
        {
          vsx_log ("SSL error for %s: %s",
                   connection->peer_address_string,
                   error->message);
          g_clear_error (&error);
          vsx_server_remove_connection (server, connection);
        }
      else if (server->gc_source == NULL)
        server->gc_source =
          vsx_main_context_add_timer (NULL, /* default context */
                                      VSX_SERVER_GC_TIMEOUT,
                                      vsx_server_gc_cb,
                                      server);
    }
}

static GSocket *
create_server_socket (GSocketAddress *address,
                      GError **error)
{
  GSocket *socket = g_socket_new (g_socket_address_get_family (address),
                                  G_SOCKET_TYPE_STREAM,
                                  G_SOCKET_PROTOCOL_DEFAULT,
                                  error);

  if (socket == NULL)
    return NULL;

  g_socket_set_blocking (socket, false);

  if (!g_socket_bind (socket, address, true, error) ||
      !g_socket_listen (socket, error))
    {
      g_object_unref (socket);
      return NULL;
    }

  return socket;
}

static GSocket *
create_socket_for_port (int port,
                        GError **error)
{
  /* First try binding it with an IPv6 address */
  GInetAddress *any_address_ipv6 =
    g_inet_address_new_any (G_SOCKET_FAMILY_IPV6);
  GSocketAddress *address_ipv6 =
    g_inet_socket_address_new (any_address_ipv6, port);

  GError *local_error = NULL;

  GSocket *socket_ipv6 = create_server_socket (address_ipv6, &local_error);

  g_object_unref (address_ipv6);
  g_object_unref (any_address_ipv6);

  if (socket_ipv6)
    return socket_ipv6;

  /* Some server try to disable IPv6 so try IPv4 in that case */
  if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED))
    {
      g_propagate_error (error, local_error);
      return NULL;
    }

  GInetAddress *any_address =
    g_inet_address_new_any (G_SOCKET_FAMILY_IPV4);
  GSocketAddress *address =
    g_inet_socket_address_new (any_address, port);

  GSocket *socket_ipv4 = create_server_socket (address, error);

  g_object_unref (any_address);
  g_object_unref (address);

  return socket_ipv4;
}

static GSocket *
create_socket_for_config (VsxConfigServer *server_config,
                          GError **error)
{
  GSocket *socket;
  int port;

  if (server_config->port == -1)
    {
      if (server_config->certificate)
        port = DEFAULT_SSL_PORT;
      else
        port = DEFAULT_PORT;
    }
  else
    {
      port = server_config->port;
    }

  if (server_config->address)
    {
      GInetAddress *address =
        g_inet_address_new_from_string (server_config->address);

      if (address == NULL)
        {
          g_set_error (error,
                       G_IO_ERROR,
                       G_IO_ERROR_INVALID_DATA,
                       "Invalid address \"%s\"",
                       server_config->address);
          return false;
        }

      GSocketAddress *socket_address =
        g_inet_socket_address_new (address, port);

      socket = create_server_socket (socket_address, error);

      g_object_unref (socket_address);
      g_object_unref (address);
    }
  else
    {
      socket = create_socket_for_port (port, error);
    }

  return socket;
}

static GSocket *
create_socket_for_fd (int fd, GError **error)
{
  GSocket *socket = g_socket_new_from_fd (fd, error);

  if (socket)
    g_socket_set_blocking (socket, false);

  return socket;
}

static int
ssl_password_cb (char *buf, int size, int rwflag, void *user_data)
{
  VsxConfigServer *server_config = user_data;

  if (server_config->private_key_password == NULL)
    return -1;

  size_t length = strlen (server_config->private_key_password);

  if (length > size)
    return -1;

  memcpy (buf, server_config->private_key_password, length);

  return length;
}

static bool
init_ssl (VsxServerSocket *ssocket,
          const VsxConfigServer *server_config,
          GError **error)
{
  ssocket->ssl_ctx = SSL_CTX_new (TLS_server_method ());
  if (ssocket->ssl_ctx == NULL)
    goto error;

  SSL_CTX_set_default_passwd_cb (ssocket->ssl_ctx, ssl_password_cb);
  SSL_CTX_set_default_passwd_cb_userdata (ssocket->ssl_ctx,
                                          (void *) server_config);

  if (SSL_CTX_use_certificate_file (ssocket->ssl_ctx,
                                    server_config->certificate,
                                    SSL_FILETYPE_PEM) <= 0)
    goto error;

  if (SSL_CTX_use_PrivateKey_file (ssocket->ssl_ctx,
                                   server_config->private_key,
                                   SSL_FILETYPE_PEM) <= 0)
    goto error;

  SSL_CTX_set_mode (ssocket->ssl_ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);

  return true;

 error:
  vsx_ssl_error_set (error);
  return false;
}

bool
vsx_server_add_config (VsxServer *server,
                       VsxConfigServer *server_config,
                       int fd_override,
                       GError **error)
{
  g_return_val_if_fail (error == NULL || *error == NULL, false);

  GSocket *socket;

  if (fd_override >= 0)
    socket = create_socket_for_fd (fd_override, error);
  else
    socket = create_socket_for_config (server_config, error);

  if (socket == NULL)
    return false;

  VsxServerSocket *ssocket = g_new0 (VsxServerSocket, 1);

  ssocket->server = server;
  ssocket->socket = socket;

  ssocket->source =
    vsx_main_context_add_poll (NULL /* default context */,
                               g_socket_get_fd (socket),
                               VSX_MAIN_CONTEXT_POLL_IN,
                               vsx_server_pending_connection_cb,
                               ssocket);

  vsx_list_insert (&server->sockets, &ssocket->link);

  if (server_config->certificate
      && !init_ssl (ssocket, server_config, error))
    {
      vsx_server_remove_socket (server, ssocket);
      return false;
    }

  return true;
}

VsxServer *
vsx_server_new (void)
{
  VsxServer *server = g_new0 (VsxServer, 1);

  server->person_set = vsx_person_set_new ();

  server->pending_conversations = vsx_conversation_set_new ();

  vsx_list_init (&server->sockets);
  vsx_list_init (&server->connections);

  return server;
}

static void
vsx_server_quit_cb (VsxMainContextSource *source,
                    void *user_data)
{
  bool *quit_received_ptr = user_data;

  *quit_received_ptr = true;

  vsx_log ("Quit signal received");
}

static void
log_server_listening (VsxServer *server)
{
  GString *buf = g_string_new (NULL);
  VsxServerSocket *ssocket;

  vsx_list_for_each (ssocket, &server->sockets, link)
    {
      if (buf->len > 0)
        {
          if (ssocket->link.next == &server->sockets)
            g_string_append (buf, " and ");
          else
            g_string_append (buf, ", ");
        }

      GSocketAddress *address =
        g_socket_get_local_address (ssocket->socket, NULL);
      char *address_string =
        get_address_string (address, true /* include_port */);

      g_string_append (buf, address_string);

      g_free (address_string);
    }

  vsx_log ("Server listening on %s", buf->str);

  g_string_free (buf, true);
}

bool
vsx_server_run (VsxServer *server,
                GError **error)
{
  VsxMainContextSource *quit_source;
  bool quit_received = false;

  /* We have to make the quit source here instead of during
     vsx_server_new because if we are daemonized then the process will
     be different by the time we reach here so the signalfd needs to
     be created in the new process */
  quit_source = vsx_main_context_add_quit (NULL /* default context */,
                                           vsx_server_quit_cb,
                                           &quit_received);

  log_server_listening (server);

  do
    vsx_main_context_poll (NULL /* default context */);
  while (!quit_received && !server->fatal_error);

  vsx_main_context_remove_source (quit_source);

  if (server->fatal_error)
    {
      g_propagate_error (error, server->fatal_error);
      server->fatal_error = NULL;

      return false;
    }
  else
    return true;
}

void
vsx_server_free (VsxServer *server)
{
  while (!vsx_list_empty (&server->connections))
    {
      VsxServerConnection *connection =
        vsx_container_of (server->connections.next, connection, link);
      vsx_server_remove_connection (server, connection);
    }

  while (!vsx_list_empty (&server->sockets))
    {
      VsxServerSocket *ssocket =
        vsx_container_of (server->sockets.next, ssocket, link);
      vsx_server_remove_socket (server, ssocket);
    }

  vsx_object_unref (server->person_set);

  vsx_object_unref (server->pending_conversations);

  g_free (server);
}
