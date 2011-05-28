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

#include <string.h>
#include "gml-error-response.h"

G_DEFINE_TYPE (GmlErrorResponse,
               gml_error_response,
               GML_TYPE_RESPONSE);

static const char
bad_request_response[] =
  "HTTP/1.1 400 Bad request\r\n"
  GML_RESPONSE_COMMON_HEADERS
  "Content-Type: text/plain; charset=ISO-8859-1\r\n"
  "Content-Length: 24\r\n"
  "\r\n"
  "The request is invalid\r\n";

static const char
unsupported_request_response[] =
  "HTTP/1.1 501 Not Implemented\r\n"
  GML_RESPONSE_COMMON_HEADERS
  "Content-Type: text/plain; charset=ISO-8859-1\r\n"
  "Content-Length: 62\r\n"
  "\r\n"
  "The client submitted a request which the server can't handle\r\n";

static const char
not_found_response[] =
  "HTTP/1.1 404 Not Found\r\n"
  GML_RESPONSE_COMMON_HEADERS
  "Content-Type: text/plain; charset=ISO-8859-1\r\n"
  "Content-Length: 47\r\n"
  "\r\n"
  "This location is not supported by this server\r\n";

static void
get_message (GmlErrorResponseType type,
             const char **message_buffer,
             unsigned int *message_length)
{
  switch (type)
    {
    case GML_ERROR_RESPONSE_BAD_REQUEST:
      *message_buffer = bad_request_response;
      *message_length = sizeof (bad_request_response) - 1;
      return;

    case GML_ERROR_RESPONSE_UNSUPPORTED_REQUEST:
      *message_buffer = unsupported_request_response;
      *message_length = sizeof (unsupported_request_response) - 1;
      return;

    case GML_ERROR_RESPONSE_NOT_FOUND:
      *message_buffer = not_found_response;
      *message_length = sizeof (not_found_response) - 1;
      return;
    }

  g_assert_not_reached ();
}

static unsigned int
gml_error_response_add_data (GmlResponse *response,
                             guint8 *data,
                             unsigned int length)
{
  GmlErrorResponse *self = GML_ERROR_RESPONSE (response);
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
gml_error_response_is_finished (GmlResponse *response)
{
  GmlErrorResponse *self = GML_ERROR_RESPONSE (response);
  const char *message_buffer;
  unsigned int message_length;

  get_message (self->type, &message_buffer, &message_length);

  return self->output_pos >= message_length;
}

static void
gml_error_response_class_init (GmlErrorResponseClass *klass)
{
  GmlResponseClass *response_class = (GmlResponseClass *) klass;

  response_class->add_data = gml_error_response_add_data;
  response_class->is_finished = gml_error_response_is_finished;
}

static void
gml_error_response_init (GmlErrorResponse *self)
{
}

GmlResponse *
gml_error_response_new (GmlErrorResponseType type)
{
  GmlErrorResponse *self =
    g_object_new (GML_TYPE_ERROR_RESPONSE, NULL);

  self->type = type;

  return GML_RESPONSE (self);
}
