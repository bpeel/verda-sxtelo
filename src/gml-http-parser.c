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
#include <errno.h>
#include <stdlib.h>

#include "gml-http-parser.h"

void
gml_http_parser_init (GmlHttpParser *parser,
                      const GmlHttpParserVtable *vtable,
                      void *user_data)

{
  parser->buf_len = 0;
  parser->state = GML_HTTP_PARSER_READING_REQUEST_LINE;
  parser->vtable = vtable;
  parser->user_data = user_data;
}

static gboolean
check_http_version (const guint8 *data,
                    unsigned int length,
                    GError **error)
{
  static const char prefix[] = "HTTP/1.";

  /* This accepts any 1.x version */

  if (length < sizeof (prefix)
      || memcmp (data, prefix, sizeof (prefix) - 1))
    goto bad;

  data += sizeof (prefix) - 1;
  length -= sizeof (prefix) - 1;

  /* The remaining characters should all be digits */
  while (length > 0)
    if (!g_ascii_isdigit (data[--length]))
      goto bad;

  return TRUE;

 bad:
  g_set_error (error,
               GML_HTTP_PARSER_ERROR,
               GML_HTTP_PARSER_ERROR_UNSUPPORTED,
               "Unsupported HTTP version");
  return FALSE;
}

static void
set_cancelled_error (GError **error)
{
  g_set_error (error,
               GML_HTTP_PARSER_ERROR,
               GML_HTTP_PARSER_ERROR_CANCELLED,
               "Application cancelled parsing");
}

static gboolean
add_bytes_to_buffer (GmlHttpParser *parser,
                     const guint8 *data,
                     unsigned int length,
                     GError **error)
{
  if (parser->buf_len + length > GML_HTTP_PARSER_MAX_LINE_LENGTH)
    {
      g_set_error (error,
                   GML_HTTP_PARSER_ERROR,
                   GML_HTTP_PARSER_ERROR_UNSUPPORTED,
                   "Unsupported line length in HTTP request");
      return FALSE;
    }
  else
    {
      memcpy (parser->buf + parser->buf_len,
              data,
              length);
      parser->buf_len += length;

      return TRUE;
    }
}

static gboolean
process_request_line (GmlHttpParser *parser,
                      guint8 *data,
                      unsigned int length,
                      GError **error)
{
  guint8 *method_end;
  guint8 *uri_end;
  const char *method = (char *) data;
  const char *uri;

  if ((method_end = memchr (data, ' ', length)) == 0)
    {
      g_set_error (error,
                   GML_HTTP_PARSER_ERROR,
                   GML_HTTP_PARSER_ERROR_INVALID,
                   "Invalid HTTP request received");
      return FALSE;
    }

  /* Replace the space with a zero terminator so we can reuse
     the buffer to pass to the callback */
  *method_end = '\0';

  length -= method_end - data + 1;
  data = method_end + 1;

  uri = (const char *) data;

  if ((uri_end = memchr (data, ' ', length)) == 0)
    {
      g_set_error (error,
                   GML_HTTP_PARSER_ERROR,
                   GML_HTTP_PARSER_ERROR_INVALID,
                   "Invalid HTTP request received");
      return FALSE;
    }

  *uri_end = '\0';

  length -= uri_end - data + 1;
  data = uri_end + 1;

  if (!check_http_version (data, length, error))
    return FALSE;

  if (!parser->vtable->request_line_received (method, uri,
                                              parser->user_data))
    {
      set_cancelled_error (error);
      return FALSE;
    }

  /* Assume there is no data unless we get a header specifying
     otherwise */
  parser->transfer_encoding = GML_HTTP_PARSER_TRANSFER_NONE;

  return TRUE;
}

static gboolean
process_header (GmlHttpParser *parser,
                GError **error)
{
  guint8 *data = parser->buf;
  unsigned int length = parser->buf_len;
  const char *field_name = (char *) data;
  const char *value;
  guint8 zero = '\0';
  guint8 *field_name_end;

  if ((field_name_end = memchr (data, ':', length)) == 0)
    {
      g_set_error (error,
                   GML_HTTP_PARSER_ERROR,
                   GML_HTTP_PARSER_ERROR_INVALID,
                   "Invalid HTTP request received");
      return FALSE;
    }

  /* Replace the colon with a zero terminator so we can reuse
     the buffer to pass to the callback */
  *field_name_end = '\0';

  length -= field_name_end - data + 1;
  data = field_name_end + 1;

  /* Skip leading spaces */
  while (length > 0 && *data == ' ')
    {
      length--;
      data++;
    }

  value = (char *) data;

  /* Add a terminator so we can pass it to the callback */
  if (!add_bytes_to_buffer (parser, &zero, 1, error))
    return FALSE;

  if (!g_ascii_strcasecmp (field_name, "content-length"))
    {
      char *tail;
      unsigned long int content_length;
      errno = 0;
      content_length = strtoul (value, &tail, 10);
      if (content_length > G_MAXUINT
          || *tail
          || errno)
        {
          g_set_error (error,
                       GML_HTTP_PARSER_ERROR,
                       GML_HTTP_PARSER_ERROR_INVALID,
                       "Invalid HTTP request received");
          return FALSE;
        }

      parser->content_length = content_length;
      parser->transfer_encoding = GML_HTTP_PARSER_TRANSFER_CONTENT_LENGTH;
    }
  else if (!g_ascii_strcasecmp (field_name, "transfer-encoding"))
    {
      if (g_ascii_strcasecmp (value, "chunked"))
        {
          g_set_error (error,
                       GML_HTTP_PARSER_ERROR,
                       GML_HTTP_PARSER_ERROR_UNSUPPORTED,
                       "Unsupported transfer-encoding \"%s\" from client",
                       value);
          return FALSE;
        }

      parser->transfer_encoding = GML_HTTP_PARSER_TRANSFER_CHUNKED;
    }

  if (!parser->vtable->header_received (field_name,
                                        value,
                                        parser->user_data))
    {
      set_cancelled_error (error);
      return FALSE;
    }

  return TRUE;
}

static gboolean
process_data (GmlHttpParser *parser,
              const guint8 *data,
              unsigned int length,
              GError **error)
{
  if (!parser->vtable->data_received (data, length, parser->user_data))
    {
      set_cancelled_error (error);
      return FALSE;
    }

  return TRUE;
}

static gboolean
process_request_finished (GmlHttpParser *parser,
                          GError **error)
{
  if (!parser->vtable->request_finished (parser->user_data))
    {
      set_cancelled_error (error);
      return FALSE;
    }

  return TRUE;
}

gboolean
gml_http_parser_parse_data (GmlHttpParser *parser,
                            const guint8 *data,
                            unsigned int length,
                            GError **error)
{
  const guint8 *terminator;

  while (length > 0)
    {
      switch (parser->state)
        {
        case GML_HTTP_PARSER_READING_REQUEST_LINE:
          /* Could the data contain a terminator? */
          if ((terminator = memchr (data, '\r', length)))
            {
              /* Add the data up to the potential terminator */
              if (!add_bytes_to_buffer (parser, data, terminator - data, error))
                return FALSE;

              /* Consume those bytes */
              length -= terminator - data + 1;
              data = terminator + 1;

              parser->state = GML_HTTP_PARSER_TERMINATING_REQUEST_LINE;
            }
          else
            {
              /* Add and consume all of the data */
              if (!add_bytes_to_buffer (parser, data, length, error))
                return FALSE;

              length = 0;
            }
          break;

        case GML_HTTP_PARSER_TERMINATING_REQUEST_LINE:
          /* Do we have the \n needed to complete the terminator? */
          if (data[0] == '\n')
            {
              /* Apparently some clients send a '\r\n' after sending
                 the request body. We can handle this by just ignoring
                 empty lines before the request line */
              if (parser->buf_len == 0)
                parser->state = GML_HTTP_PARSER_READING_REQUEST_LINE;
              else
                {
                  if (!process_request_line (parser,
                                             parser->buf,
                                             parser->buf_len,
                                             error))
                    return FALSE;

                  parser->buf_len = 0;
                  /* Start processing headers */
                  parser->state = GML_HTTP_PARSER_READING_HEADER;
                }

              /* Consume the \n */
              data++;
              length--;
            }
          else
            {
              guint8 r = '\r';
              /* Add the \r that we ignored when switching to this
                 state and then switch back to reading the request
                 line without consuming the char */
              if (!add_bytes_to_buffer (parser, &r, 1, error))
                return FALSE;
              parser->state = GML_HTTP_PARSER_READING_REQUEST_LINE;
            }
          break;

        case GML_HTTP_PARSER_READING_HEADER:
          /* Could the data contain a terminator? */
          if ((terminator = memchr (data, '\r', length)))
            {
              /* Add the data up to the potential terminator */
              if (!add_bytes_to_buffer (parser, data, terminator - data, error))
                return FALSE;

              /* Consume those bytes */
              length -= terminator - data + 1;
              data = terminator + 1;

              parser->state = GML_HTTP_PARSER_TERMINATING_HEADER;
            }
          else
            {
              /* Add and consume all of the data */
              if (!add_bytes_to_buffer (parser, data, length, error))
                return FALSE;

              length = 0;
            }
          break;

        case GML_HTTP_PARSER_TERMINATING_HEADER:
          /* Do we have the \n needed to complete the terminator? */
          if (data[0] == '\n')
            {
              /* If the header is empty then this marks the end of the
                 headers */
              if (parser->buf_len == 0)
                switch (parser->transfer_encoding)
                  {
                  case GML_HTTP_PARSER_TRANSFER_NONE:
                    /* The request is finished */
                    if (!process_request_finished (parser, error))
                      return FALSE;
                    parser->buf_len = 0;
                    parser->state = GML_HTTP_PARSER_READING_REQUEST_LINE;
                    break;

                  case GML_HTTP_PARSER_TRANSFER_CONTENT_LENGTH:
                    parser->state = GML_HTTP_PARSER_READING_DATA_WITH_LENGTH;
                    break;

                  case GML_HTTP_PARSER_TRANSFER_CHUNKED:
                    parser->state = GML_HTTP_PARSER_READING_CHUNK_LENGTH;
                    parser->content_length = 0;
                    break;
                  }
              else
                /* Start checking for a continuation */
                parser->state = GML_HTTP_PARSER_CHECKING_HEADER_CONTINUATION;

              /* Consume the \n */
              data++;
              length--;
            }
          else
            {
              guint8 r = '\r';
              /* Add the \r that we ignored when switching to this
                 state and then switch back to reading the header
                 without consuming the char */
              if (!add_bytes_to_buffer (parser, &r, 1, error))
                return FALSE;
              parser->state = GML_HTTP_PARSER_READING_HEADER;
            }
          break;

        case GML_HTTP_PARSER_CHECKING_HEADER_CONTINUATION:
          /* Do we have a continuation character? */
          if (data[0] == ' ')
            {
              /* Yes, continue processing headers */
              parser->state = GML_HTTP_PARSER_READING_HEADER;
              /* We don't consume the character so that the space will
                 be added to the buffer */
            }
          else
            {
              /* We have a complete header */
              if (!process_header (parser, error))
                return FALSE;

              parser->buf_len = 0;
              parser->state = GML_HTTP_PARSER_READING_HEADER;
            }
          break;

        case GML_HTTP_PARSER_READING_DATA_WITH_LENGTH:
          {
            unsigned int to_process_length = MIN (parser->content_length,
                                                  length);

            if (!process_data (parser, data, to_process_length, error))
              return FALSE;

            parser->content_length -= to_process_length;
            data += to_process_length;
            length -= to_process_length;

            if (parser->content_length == 0)
              {
                /* The request is finished */
                if (!process_request_finished (parser, error))
                  return FALSE;
                parser->buf_len = 0;
                parser->state = GML_HTTP_PARSER_READING_REQUEST_LINE;
              }
          }
          break;

        case GML_HTTP_PARSER_READING_CHUNK_LENGTH:
          if (g_ascii_isdigit (*data))
            {
              unsigned int new_length;

              new_length = parser->content_length * 10 + *data - '0';

              if (new_length < parser->content_length)
                {
                  g_set_error (error,
                               GML_HTTP_PARSER_ERROR,
                               GML_HTTP_PARSER_ERROR_INVALID,
                               "Invalid chunk length received");
                  return FALSE;
                }

              parser->content_length = new_length;

              /* Consume the digit */
              data++;
              length--;
            }
          else if (*data == ';')
            parser->state = GML_HTTP_PARSER_IGNORING_CHUNK_EXTENSION;
          else if (*data == '\r')
            {
              data++;
              length--;
              parser->state = GML_HTTP_PARSER_TERMINATING_CHUNK_LENGTH;
            }
          else
            {
              g_set_error (error,
                           GML_HTTP_PARSER_ERROR,
                           GML_HTTP_PARSER_ERROR_INVALID,
                           "Invalid chunk length received");
              return FALSE;
            }
          break;

        case GML_HTTP_PARSER_TERMINATING_CHUNK_LENGTH:
          if (*data != '\n')
            {
              g_set_error (error,
                           GML_HTTP_PARSER_ERROR,
                           GML_HTTP_PARSER_ERROR_INVALID,
                           "Invalid chunk length received");
              return FALSE;
            }
          data++;
          length--;
          if (parser->content_length)
            parser->state = GML_HTTP_PARSER_READING_CHUNK;
          else
            parser->state = GML_HTTP_PARSER_IGNORING_CHUNK_TRAILER;
          break;

        case GML_HTTP_PARSER_IGNORING_CHUNK_EXTENSION:
          /* Could the data contain a terminator? */
          if ((terminator = memchr (data, '\r', length)))
            {
              parser->state = GML_HTTP_PARSER_TERMINATING_CHUNK_EXTENSION;
              length -= terminator - data + 1;
              data = terminator + 1;
            }
          else
            /* Consume all of the data */
            length = 0;
          break;

        case GML_HTTP_PARSER_TERMINATING_CHUNK_EXTENSION:
          if (*data == '\n')
            {
              data++;
              length--;
              if (parser->content_length)
                parser->state = GML_HTTP_PARSER_READING_CHUNK;
              else
                parser->state = GML_HTTP_PARSER_IGNORING_CHUNK_TRAILER;
            }
          else
            parser->state = GML_HTTP_PARSER_IGNORING_CHUNK_EXTENSION;
          break;

        case GML_HTTP_PARSER_IGNORING_CHUNK_TRAILER:
          /* Could the data contain a terminator? */
          if ((terminator = memchr (data, '\r', length)))
            {
              parser->state = GML_HTTP_PARSER_TERMINATING_CHUNK_TRAILER;
              parser->content_length += terminator - data;
              length -= terminator - data + 1;
              data = terminator + 1;
            }
          else
            /* Consume all of the data */
            length = 0;
          break;

        case GML_HTTP_PARSER_TERMINATING_CHUNK_TRAILER:
          if (*data == '\n')
            {
              length--;
              data++;
              /* A blank line marks the end of the trailer and thus
                 the request also */
              if (parser->content_length == 0)
                {
                  if (!process_request_finished (parser, error))
                    return FALSE;
                  parser->buf_len = 0;
                  parser->state = GML_HTTP_PARSER_READING_REQUEST_LINE;
                }
              else
                {
                  parser->content_length = 0;
                  parser->state = GML_HTTP_PARSER_IGNORING_CHUNK_TRAILER;
                }
            }
          else
            {
              /* Count one character for the '\r' */
              parser->content_length++;
              parser->state = GML_HTTP_PARSER_IGNORING_CHUNK_TRAILER;
            }
          break;

        case GML_HTTP_PARSER_READING_CHUNK:
          {
            unsigned int to_process_length = MIN (parser->content_length,
                                                  length);

            if (!process_data (parser, data, to_process_length, error))
              return FALSE;

            parser->content_length -= to_process_length;
            data += to_process_length;
            length -= to_process_length;

            if (parser->content_length == 0)
              /* The chunk is finished */
              parser->state = GML_HTTP_PARSER_READING_CHUNK_TERMINATOR1;
          }
          break;

        case GML_HTTP_PARSER_READING_CHUNK_TERMINATOR1:
          {
            if (*data != '\r')
              g_set_error (error,
                           GML_HTTP_PARSER_ERROR,
                           GML_HTTP_PARSER_ERROR_INVALID,
                           "Invalid chunk terminator received");

            data++;
            length--;

            parser->state = GML_HTTP_PARSER_READING_CHUNK_TERMINATOR2;
          }
          break;

        case GML_HTTP_PARSER_READING_CHUNK_TERMINATOR2:
          {
            if (*data != '\n')
              g_set_error (error,
                           GML_HTTP_PARSER_ERROR,
                           GML_HTTP_PARSER_ERROR_INVALID,
                           "Invalid chunk terminator received");

            data++;
            length--;

            parser->state = GML_HTTP_PARSER_READING_CHUNK_LENGTH;
          }
          break;
        }
    }

  return TRUE;
}

gboolean
gml_http_parser_parser_eof (GmlHttpParser *parser,
                            GError **error)
{
  switch (parser->state)
    {
    case GML_HTTP_PARSER_READING_REQUEST_LINE:
      /* This is an acceptable place for the client to shutdown the
         connection if there we haven't received any of the line
         yet */
      if (parser->buf_len == 0)
        return TRUE;

      /* flow through */
    case GML_HTTP_PARSER_TERMINATING_REQUEST_LINE:
    case GML_HTTP_PARSER_READING_HEADER:
    case GML_HTTP_PARSER_TERMINATING_HEADER:
    case GML_HTTP_PARSER_CHECKING_HEADER_CONTINUATION:
    case GML_HTTP_PARSER_READING_DATA_WITH_LENGTH:
    case GML_HTTP_PARSER_READING_CHUNK_LENGTH:
    case GML_HTTP_PARSER_TERMINATING_CHUNK_LENGTH:
    case GML_HTTP_PARSER_IGNORING_CHUNK_EXTENSION:
    case GML_HTTP_PARSER_TERMINATING_CHUNK_EXTENSION:
    case GML_HTTP_PARSER_IGNORING_CHUNK_TRAILER:
    case GML_HTTP_PARSER_TERMINATING_CHUNK_TRAILER:
    case GML_HTTP_PARSER_READING_CHUNK:
    case GML_HTTP_PARSER_READING_CHUNK_TERMINATOR1:
    case GML_HTTP_PARSER_READING_CHUNK_TERMINATOR2:
      /* Closing the connection here is invalid */
      g_set_error (error,
                   GML_HTTP_PARSER_ERROR,
                   GML_HTTP_PARSER_ERROR_INVALID,
                   "Client closed the connection unexpectedly");
      return FALSE;
    }

  g_assert_not_reached ();
}

GQuark
gml_http_parser_error_quark (void)
{
  return g_quark_from_static_string ("gml-http-parser-error");
}
