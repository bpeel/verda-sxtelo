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

#include <string.h>
#include "vsx-string-response.h"

static const char
bad_request_response[] =
  "HTTP/1.1 400 Bad request\r\n"
  VSX_RESPONSE_COMMON_HEADERS
  "Content-Type: text/plain; charset=ISO-8859-1\r\n"
  "Content-Length: 24\r\n"
  "\r\n"
  "The request is invalid\r\n";

static const char
unsupported_request_response[] =
  "HTTP/1.1 501 Not Implemented\r\n"
  VSX_RESPONSE_COMMON_HEADERS
  "Content-Type: text/plain; charset=ISO-8859-1\r\n"
  "Content-Length: 62\r\n"
  "\r\n"
  "The client submitted a request which the server can't handle\r\n";

static const char
not_found_response[] =
  "HTTP/1.1 404 Not Found\r\n"
  VSX_RESPONSE_COMMON_HEADERS
  "Content-Type: text/plain; charset=ISO-8859-1\r\n"
  "Content-Length: 47\r\n"
  "\r\n"
  "This location is not supported by this server\r\n";

static const char
request_timeout[] =
  "HTTP/1.1 408 Request Timeout\r\n"
  VSX_RESPONSE_COMMON_HEADERS
  "Content-Type: text/plain; charset=ISO-8859-1\r\n"
  "Content-Length: 54\r\n"
  "\r\n"
  "No request appeared within a reasonable time period.\r\n";

static const char
preflight_post_ok[] =
  "HTTP/1.1 200 OK\r\n"
  VSX_RESPONSE_COMMON_HEADERS
  "Access-Control-Allow-Headers: Content-Type\r\n"
  "Content-Length: 0\r\n"
  "\r\n";

static const char
ok_response[] =
  "HTTP/1.1 200 OK\r\n"
  VSX_RESPONSE_COMMON_HEADERS
  VSX_RESPONSE_DISABLE_CACHE_HEADERS
  "Content-Type: text/plain; charset=ISO-8859-1\r\n"
  "Content-Length: 4\r\n"
  "\r\n"
  "OK\r\n";

static void
get_message (VsxStringResponseType type,
             const char **message_buffer,
             unsigned int *message_length)
{
  switch (type)
    {
    case VSX_STRING_RESPONSE_BAD_REQUEST:
      *message_buffer = bad_request_response;
      *message_length = sizeof (bad_request_response) - 1;
      return;

    case VSX_STRING_RESPONSE_UNSUPPORTED_REQUEST:
      *message_buffer = unsupported_request_response;
      *message_length = sizeof (unsupported_request_response) - 1;
      return;

    case VSX_STRING_RESPONSE_NOT_FOUND:
      *message_buffer = not_found_response;
      *message_length = sizeof (not_found_response) - 1;
      return;

    case VSX_STRING_RESPONSE_PREFLIGHT_POST_OK:
      *message_buffer = preflight_post_ok;
      *message_length = sizeof (preflight_post_ok) - 1;
      return;

    case VSX_STRING_RESPONSE_REQUEST_TIMEOUT:
      *message_buffer = request_timeout;
      *message_length = sizeof (request_timeout) - 1;
      return;

    case VSX_STRING_RESPONSE_OK:
      *message_buffer = ok_response;
      *message_length = sizeof (ok_response) - 1;
      return;
    }

  g_assert_not_reached ();
}

static unsigned int
vsx_string_response_add_data (VsxResponse *response,
                              guint8 *data,
                              unsigned int length)
{
  VsxStringResponse *self = (VsxStringResponse *) response;
  const char *message_buffer;
  unsigned int message_length;
  unsigned int to_write;

  get_message (self->type, &message_buffer, &message_length);

  to_write = MIN (length, message_length - self->output_pos);
  memcpy (data, message_buffer + self->output_pos, to_write);

  self->output_pos += to_write;

  return to_write;
}

static gboolean
vsx_string_response_is_finished (VsxResponse *response)
{
  VsxStringResponse *self = (VsxStringResponse *) response;
  const char *message_buffer;
  unsigned int message_length;

  get_message (self->type, &message_buffer, &message_length);

  return self->output_pos >= message_length;
}

static VsxResponseClass *
vsx_string_response_get_class (void)
{
  static VsxResponseClass klass;

  if (klass.parent_class.free == NULL)
    {
      klass = *vsx_response_get_class ();

      klass.parent_class.instance_size = sizeof (VsxStringResponse);

      klass.add_data = vsx_string_response_add_data;
      klass.is_finished = vsx_string_response_is_finished;
    }

  return &klass;
}

VsxResponse *
vsx_string_response_new (VsxStringResponseType type)
{
  VsxStringResponse *self =
    vsx_object_allocate (vsx_string_response_get_class ());

  vsx_response_init (self);

  self->type = type;

  return (VsxResponse *) self;
}
