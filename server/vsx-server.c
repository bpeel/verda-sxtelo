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

#include "config.h"

#include <string.h>
#include <sys/socket.h>
#include <errno.h>
#include <openssl/ssl.h>
#include <unistd.h>
#include <assert.h>

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
#include "vsx-buffer.h"
#include "vsx-file-error.h"
#include "vsx-netaddress.h"
#include "vsx-socket.h"

#define DEFAULT_PORT 5144
#define DEFAULT_SSL_PORT (DEFAULT_PORT + 1)

struct _VsxServer
{
  /* List of VsxServerSockets */
  struct vsx_list sockets;

  /* If this gets set then vsx_server_run will return and report the
     error */
  struct vsx_error *fatal_error;

  /* List of open connections */
  struct vsx_list connections;

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

  int client_socket;
  VsxMainContextSource *source;

  /* List node within the list of connections */
  struct vsx_list link;

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
  struct vsx_list link;
  VsxMainContextSource *source;
  int sock;
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

struct vsx_error_domain
vsx_server_error;

static void
ws_connection_changed_cb (VsxListener *listener,
                          void *data)
{
  VsxServerConnection *connection =
    vsx_container_of (listener, VsxServerConnection, ws_connection_listener);

  update_poll (connection);
}

static void
set_bad_input (VsxServerConnection *connection)
{
  connection->had_bad_input = true;
}

static void
set_bad_input_with_error (VsxServerConnection *connection,
                          struct vsx_error *error)
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
  vsx_close (connection->client_socket);
  vsx_list_remove (&connection->link);
  vsx_free (connection->peer_address_string);

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

  if (ssocket->sock != -1)
    vsx_close (ssocket->sock);

  vsx_list_remove (&ssocket->link);

  vsx_free (ssocket);
}

static void
log_ssl_error (VsxServerConnection *connection)
{
  struct vsx_error *error = NULL;

  vsx_ssl_error_set (&error);
  vsx_log ("For %s: %s", connection->peer_address_string, error->message);
  vsx_error_free (error);
}

static bool
is_would_block_error (int err)
{
  return err == EAGAIN || err == EWOULDBLOCK;
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
          if (shutdown (connection->client_socket, SHUT_WR) == -1)
            {
              vsx_log ("shutdown socket failed for %s: %s",
                       connection->peer_address_string,
                       strerror (errno));
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
  if (connection->read_finished)
    {
      /* This might happen if the SSL_Shutdown command triggered a
       * poll for input */
      update_poll (connection);
      return;
    }

  char buf[1024];

  ssize_t got;

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
      got = read (connection->client_socket,
                  buf,
                  sizeof (buf));
      if (got == -1)
        {
          if (!is_would_block_error (errno) && errno != EINTR)
            {
              vsx_log ("Error reading from socket for %s: %s",
                       connection->peer_address_string,
                       strerror (errno));
              vsx_server_remove_connection (server, connection);
            }

          return;
        }
    }

  if (got == 0)
    {
      if (!connection->had_bad_input)
        {
          struct vsx_error *ws_error = NULL;

          if (!vsx_connection_parse_eof (connection->ws_connection, &ws_error))
            {
              set_bad_input_with_error (connection, ws_error);
              vsx_error_free (ws_error);
            }
        }

      connection->read_finished = true;

      update_poll (connection);
    }
  else
    {
      struct vsx_error *ws_error = NULL;

      if (!connection->had_bad_input
          && !vsx_connection_parse_data (connection->ws_connection,
                                         (uint8_t *) buf,
                                         got,
                                         &ws_error))
        {
          set_bad_input_with_error (connection, ws_error);
          vsx_error_free (ws_error);
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
  ssize_t wrote;

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
      wrote = write (connection->client_socket,
                     (const char *) connection->output_buffer,
                     connection->output_length);

      if (wrote == -1)
        {
          if (!is_would_block_error (errno) && errno != EINTR)
            {
              vsx_log ("Error writing to socket for %s: %s",
                       connection->peer_address_string,
                       strerror (errno));
              vsx_server_remove_connection (server, connection);
            }

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

      if (getsockopt (connection->client_socket,
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

static bool
init_connection_ssl (VsxServerConnection *connection,
                     SSL_CTX *ssl_ctx,
                     struct vsx_error **error)
{
  connection->ssl = SSL_new (ssl_ctx);

  if (connection->ssl == NULL)
    goto error;

  SSL_set_accept_state (connection->ssl);

  if (!SSL_set_fd (connection->ssl, connection->client_socket))
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

  struct vsx_netaddress_native native_address =
    {
      .length = offsetof (struct vsx_netaddress_native, length)
    };

  int client_socket = accept (ssocket->sock,
                              &native_address.sockaddr,
                              &native_address.length);

  if (client_socket == -1)
    {
      /* Ignore WOULD_BLOCK and EINTR errors */
      if (is_would_block_error (errno) || errno == EINTR)
        return;

      if (errno == EMFILE)
        {
          vsx_log ("Too many open files to accept connection");

          /* Stop listening for new connections until someone disconnects */
          vsx_main_context_modify_poll (source, 0);
          return;
        }

      /* This will cause vsx_server_run to return */
      vsx_file_error_set (&server->fatal_error,
                          errno,
                          "Error accepting connection: %s",
                          strerror (errno));

      return;
    }

  struct vsx_error *error = NULL;

  if (!vsx_socket_set_nonblock (client_socket, &error))
    {
      vsx_log ("While accepting connection: %s", error->message);
      vsx_error_free (error);
      return;
    }

  VsxServerConnection *connection = vsx_alloc (sizeof *connection);

  connection->server = server;
  connection->client_socket = client_socket;
  connection->source =
    vsx_main_context_add_poll (NULL /* default context */,
                               client_socket,
                               VSX_MAIN_CONTEXT_POLL_IN,
                               vsx_server_connection_poll_cb,
                               connection);
  vsx_list_insert (&server->connections, &connection->link);

  struct vsx_netaddress remote_address;
  vsx_netaddress_from_native (&remote_address, &native_address);

  connection->ws_connection =
    vsx_connection_new (&remote_address,
                        server->pending_conversations,
                        server->person_set);

  VsxSignal *changed_signal =
    vsx_connection_get_changed_signal (connection->ws_connection);
  connection->ws_connection_listener.notify =
    ws_connection_changed_cb;
  vsx_signal_add (changed_signal,
                  &connection->ws_connection_listener);

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
      connection->peer_address_string =
        vsx_netaddress_to_string (&remote_address);
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
      vsx_error_free (error);
      vsx_server_remove_connection (server, connection);
    }
  else if (server->gc_source == NULL)
    {
      server->gc_source =
        vsx_main_context_add_timer (NULL, /* default context */
                                    VSX_SERVER_GC_TIMEOUT,
                                    vsx_server_gc_cb,
                                    server);
    }
}

static int
create_socket_for_address (const struct vsx_netaddress *address,
                           struct vsx_error **error)
{
  struct vsx_netaddress_native native_address;

  vsx_netaddress_to_native (address, &native_address);

  int sock = socket (native_address.sockaddr.sa_family == AF_INET6
                     ? PF_INET6
                     : PF_INET,
                     SOCK_STREAM,
                     0);

  if (sock == -1)
    {
      vsx_file_error_set (error,
                          errno,
                          "Failed to create socket: %s",
                          strerror (errno));
      return -1;
    }

  const int true_value = true;

  setsockopt (sock,
              SOL_SOCKET, SO_REUSEADDR,
              &true_value, sizeof true_value);

  if (!vsx_socket_set_nonblock (sock, error))
    goto error;

  if (bind (sock,
            &native_address.sockaddr,
            native_address.length) == -1)
    {
      vsx_file_error_set (error,
                          errno,
                          "Failed to bind socket: %s",
                          strerror (errno));
      goto error;
    }

  if (listen (sock, 10) == -1)
    {
      vsx_file_error_set (error,
                          errno,
                          "Failed to make socket listen: %s",
                          strerror (errno));
      goto error;
    }

  return sock;

 error:
  vsx_close(sock);
  return -1;
}

static int
create_socket_for_port (int port,
                        struct vsx_error **error)
{
  struct vsx_netaddress netaddress;

  memset (&netaddress, 0, sizeof netaddress);

  /* First try binding it with an IPv6 address */
  netaddress.port = port;
  netaddress.family = AF_INET6;

  struct vsx_error *local_error = NULL;

  int sock = create_socket_for_address (&netaddress, &local_error);

  if (sock != -1)
    return sock;

  if (local_error->domain == &vsx_file_error
      && (local_error->code == VSX_FILE_ERROR_PFNOSUPPORT
          || local_error->code == VSX_FILE_ERROR_AFNOSUPPORT))
    {
      vsx_error_free (local_error);
    }
  else
    {
      vsx_error_propagate (error, local_error);
      return -1;
    }

  /* Some servers disable IPv6 so try IPv4 */
  netaddress.family = AF_INET;

  return create_socket_for_address (&netaddress, error);
}

static int
create_socket_for_config (VsxConfigServer *server_config,
                          struct vsx_error **error)
{
  int default_port;

  if (server_config->port == -1)
    {
      default_port = (server_config->certificate
                      ? DEFAULT_SSL_PORT
                      : DEFAULT_PORT);
    }
  else
    {
      default_port = server_config->port;
    }

  if (server_config->address)
    {
      struct vsx_netaddress address;

      if (!vsx_netaddress_from_string (&address,
                                       server_config->address,
                                       default_port))
        {
          vsx_set_error (error,
                         &vsx_server_error,
                         VSX_SERVER_ERROR_INVALID_ADDRESS,
                         "Invalid address \"%s\"",
                         server_config->address);
          return -1;
        }

      return create_socket_for_address (&address, error);
    }
  else
    {
      return create_socket_for_port (default_port, error);
    }
}

static int
create_socket_for_fd (int fd, struct vsx_error **error)
{
  if (!vsx_socket_set_nonblock (fd, error))
    return -1;

  return fd;
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
          struct vsx_error **error)
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
                       struct vsx_error **error)
{
  assert (error == NULL || *error == NULL);

  int sock;

  if (fd_override >= 0)
    sock = create_socket_for_fd (fd_override, error);
  else
    sock = create_socket_for_config (server_config, error);

  if (sock == -1)
      return false;

  VsxServerSocket *ssocket = vsx_calloc (sizeof *ssocket);

  ssocket->server = server;
  ssocket->sock = sock;

  ssocket->source =
    vsx_main_context_add_poll (NULL /* default context */,
                               sock,
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
  VsxServer *server = vsx_calloc (sizeof *server);

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
  struct vsx_buffer buf = VSX_BUFFER_STATIC_INIT;

  VsxServerSocket *ssocket;

  vsx_list_for_each (ssocket, &server->sockets, link)
    {
      if (buf.length > 0)
        {
          if (ssocket->link.next == &server->sockets)
            vsx_buffer_append_string (&buf, " and ");
          else
            vsx_buffer_append_string (&buf, ", ");
        }

      struct vsx_netaddress_native native_address =
        {
          .length = offsetof (struct vsx_netaddress_native, length),
        };

      if (getsockname (ssocket->sock,
                       &native_address.sockaddr,
                       &native_address.length) == -1)
        {
          vsx_buffer_append_string (&buf, "?");
        }
      else
        {
          struct vsx_netaddress address;
          vsx_netaddress_from_native (&address, &native_address);

          char *address_string = vsx_netaddress_to_string (&address);
          vsx_buffer_append_string (&buf, address_string);
          vsx_free (address_string);
        }
    }

  vsx_log ("Server listening on %s", (const char *) buf.data);

  vsx_buffer_destroy (&buf);
}

bool
vsx_server_run (VsxServer *server,
                struct vsx_error **error)
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
      vsx_error_propagate (error, server->fatal_error);
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
        vsx_container_of (server->connections.next, VsxServerConnection, link);
      vsx_server_remove_connection (server, connection);
    }

  while (!vsx_list_empty (&server->sockets))
    {
      VsxServerSocket *ssocket =
        vsx_container_of (server->sockets.next, VsxServerSocket, link);
      vsx_server_remove_socket (server, ssocket);
    }

  vsx_object_unref (server->person_set);

  vsx_object_unref (server->pending_conversations);

  vsx_free (server);
}
