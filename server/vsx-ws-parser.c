/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2011, 2015, 2020, 2021  Neil Roberts
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

#include "vsx-ws-parser.h"

#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>

#define VSX_WS_PARSER_MAX_LINE_LENGTH 512

struct _VsxWsParser
{
  unsigned int buf_len;

  guint8 buf[VSX_WS_PARSER_MAX_LINE_LENGTH];

  enum
  {
    VSX_WS_PARSER_READING_REQUEST_LINE,
    VSX_WS_PARSER_TERMINATING_REQUEST_LINE,
    VSX_WS_PARSER_READING_HEADER,
    VSX_WS_PARSER_TERMINATING_HEADER,
    VSX_WS_PARSER_CHECKING_HEADER_CONTINUATION,
    VSX_WS_PARSER_DONE
  } state;

  const VsxWsParserVtable *vtable;
  void *user_data;
};

VsxWsParser *
vsx_ws_parser_new (const VsxWsParserVtable *vtable,
                   void *user_data)
{
  VsxWsParser *parser = g_new (VsxWsParser, 1);

  parser->buf_len = 0;
  parser->state = VSX_WS_PARSER_READING_REQUEST_LINE;
  parser->vtable = vtable;
  parser->user_data = user_data;

  return parser;
}

static gboolean
check_http_version (const guint8 *data, unsigned int length, GError **error)
{
  static const char prefix[] = "HTTP/1.";

  /* This accepts any 1.x version */

  if (length < sizeof (prefix) || memcmp (data, prefix, sizeof (prefix) - 1))
    goto bad;

  data += sizeof (prefix) - 1;
  length -= sizeof (prefix) - 1;

  /* The remaining characters should all be digits */
  while (length > 0)
    {
      if (!g_ascii_isdigit (data[--length]))
        goto bad;
    }

  return TRUE;

bad:
  g_set_error (error,
               VSX_WS_PARSER_ERROR,
               VSX_WS_PARSER_ERROR_UNSUPPORTED,
               "Unsupported HTTP version");
  return FALSE;
}

static void
set_cancelled_error (GError **error)
{
  g_set_error (error,
               VSX_WS_PARSER_ERROR,
               VSX_WS_PARSER_ERROR_CANCELLED,
               "Application cancelled parsing");
}

static gboolean
add_bytes_to_buffer (VsxWsParser *parser,
                     const guint8 *data,
                     unsigned int length,
                     GError **error)
{
  if (parser->buf_len + length > VSX_WS_PARSER_MAX_LINE_LENGTH)
    {
      g_set_error (error,
                   VSX_WS_PARSER_ERROR,
                   VSX_WS_PARSER_ERROR_UNSUPPORTED,
                   "Unsupported line length in HTTP request");
      return FALSE;
    }
  else
    {
      memcpy (parser->buf + parser->buf_len, data, length);
      parser->buf_len += length;

      return TRUE;
    }
}

static gboolean
process_request_line (VsxWsParser *parser,
                      guint8 *data,
                      unsigned int length,
                      GError **error)
{
  guint8 *method_end;
  guint8 *uri_end;
  const char *method = (char *) data;
  const char *uri;

  method_end = memchr (data, ' ', length);

  if (method_end == NULL)
    {
      g_set_error (error,
                   VSX_WS_PARSER_ERROR,
                   VSX_WS_PARSER_ERROR_INVALID,
                   "Invalid HTTP request received");
      return FALSE;
    }

  /* Replace the space with a zero terminator so we can reuse
   * the buffer to pass to the callback
   */
  *method_end = '\0';

  length -= method_end - data + 1;
  data = method_end + 1;

  uri = (const char *) data;
  uri_end = memchr (data, ' ', length);

  if (uri_end == NULL)
    {
      g_set_error (error,
                   VSX_WS_PARSER_ERROR,
                   VSX_WS_PARSER_ERROR_INVALID,
                   "Invalid HTTP request received");
      return FALSE;
    }

  *uri_end = '\0';

  length -= uri_end - data + 1;
  data = uri_end + 1;

  if (!check_http_version (data, length, error))
    return FALSE;

  if (!parser->vtable->request_line_received (method, uri, parser->user_data))
    {
      set_cancelled_error (error);
      return FALSE;
    }

  return TRUE;
}

static gboolean
process_header (VsxWsParser *parser, GError **error)
{
  guint8 *data = parser->buf;
  unsigned int length = parser->buf_len;
  const char *field_name = (char *) data;
  const char *value;
  guint8 zero = '\0';
  guint8 *field_name_end;

  field_name_end = memchr (data, ':', length);

  if (field_name_end == NULL)
    {
      g_set_error (error,
                   VSX_WS_PARSER_ERROR,
                   VSX_WS_PARSER_ERROR_INVALID,
                   "Invalid HTTP request received");
      return FALSE;
    }

  /* Replace the colon with a zero terminator so we can reuse
   * the buffer to pass to the callback
   */
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

  if (!parser->vtable->header_received (field_name, value, parser->user_data))
    {
      set_cancelled_error (error);
      return FALSE;
    }

  return TRUE;
}

typedef struct
{
  const guint8 *data;
  unsigned int length;
} VsxWsParserClosure;

static gboolean
handle_reading_request_line (VsxWsParser *parser,
                             VsxWsParserClosure *c,
                             GError **error)
{
  const guint8 *terminator;

  /* Could the data contain a terminator? */
  if ((terminator = memchr (c->data, '\r', c->length)))
    {
      /* Add the data up to the potential terminator */
      if (!add_bytes_to_buffer (parser, c->data, terminator - c->data, error))
        return FALSE;

      /* Consume those bytes */
      c->length -= terminator - c->data + 1;
      c->data = terminator + 1;

      parser->state = VSX_WS_PARSER_TERMINATING_REQUEST_LINE;
    }
  else
    {
      /* Add and consume all of the data */
      if (!add_bytes_to_buffer (parser, c->data, c->length, error))
        return FALSE;

      c->length = 0;
    }

  return TRUE;
}

static gboolean
handle_terminating_request_line (VsxWsParser *parser,
                                 VsxWsParserClosure *c,
                                 GError **error)
{
  /* Do we have the \n needed to complete the terminator? */
  if (c->data[0] == '\n')
    {
      /* Apparently some clients send a '\r\n' after sending
       * the request body. We can handle this by just
       * ignoring empty lines before the request line
       */
      if (parser->buf_len == 0)
        parser->state = VSX_WS_PARSER_READING_REQUEST_LINE;
      else
        {
          if (!process_request_line (parser,
                                     parser->buf,
                                     parser->buf_len,
                                     error))
            return FALSE;

          parser->buf_len = 0;
          /* Start processing headers */
          parser->state = VSX_WS_PARSER_READING_HEADER;
        }

      /* Consume the \n */
      c->data++;
      c->length--;
    }
  else
    {
      guint8 r = '\r';
      /* Add the \r that we ignored when switching to this
       * state and then switch back to reading the request
       * line without consuming the char
       */
      if (!add_bytes_to_buffer (parser, &r, 1, error))
        return FALSE;
      parser->state = VSX_WS_PARSER_READING_REQUEST_LINE;
    }

  return TRUE;
}

static gboolean
handle_reading_header (VsxWsParser *parser,
                       VsxWsParserClosure *c,
                       GError **error)
{
  const guint8 *terminator;

  /* Could the data contain a terminator? */
  if ((terminator = memchr (c->data, '\r', c->length)))
    {
      /* Add the data up to the potential terminator */
      if (!add_bytes_to_buffer (parser, c->data, terminator - c->data, error))
        return FALSE;

      /* Consume those bytes */
      c->length -= terminator - c->data + 1;
      c->data = terminator + 1;

      parser->state = VSX_WS_PARSER_TERMINATING_HEADER;
    }
  else
    {
      /* Add and consume all of the data */
      if (!add_bytes_to_buffer (parser, c->data, c->length, error))
        return FALSE;

      c->length = 0;
    }

  return TRUE;
}

static gboolean
handle_terminating_header (VsxWsParser *parser,
                           VsxWsParserClosure *c,
                           GError **error)
{
  /* Do we have the \n needed to complete the terminator? */
  if (c->data[0] == '\n')
    {
      /* If the header is empty then this marks the end of the
       * headers
       */
      if (parser->buf_len == 0)
        {
          parser->state = VSX_WS_PARSER_DONE;
        }
      else
        {
          /* Start checking for a continuation */
          parser->state = VSX_WS_PARSER_CHECKING_HEADER_CONTINUATION;
        }

      /* Consume the \n */
      c->data++;
      c->length--;
    }
  else
    {
      guint8 r = '\r';
      /* Add the \r that we ignored when switching to this
       * state and then switch back to reading the header
       * without consuming the char
       */
      if (!add_bytes_to_buffer (parser, &r, 1, error))
        return FALSE;
      parser->state = VSX_WS_PARSER_READING_HEADER;
    }

  return TRUE;
}

static gboolean
handle_checking_header_continuation (VsxWsParser *parser,
                                     VsxWsParserClosure *c,
                                     GError **error)
{
  /* Do we have a continuation character? */
  if (c->data[0] == ' ')
    {
      /* Yes, continue processing headers */
      parser->state = VSX_WS_PARSER_READING_HEADER;
      /* We don't consume the character so that the space
       * will be added to the buffer
       */
    }
  else
    {
      /* We have a complete header */
      if (!process_header (parser, error))
        return FALSE;

      parser->buf_len = 0;
      parser->state = VSX_WS_PARSER_READING_HEADER;
    }

  return TRUE;
}

VsxWsParserResult
vsx_ws_parser_parse_data (VsxWsParser *parser,
                          const guint8 *data,
                          size_t length,
                          size_t *consumed,
                          GError **error)
{
  VsxWsParserClosure closure;

  closure.data = data;
  closure.length = length;

  while (closure.length > 0)
    {
      switch (parser->state)
        {
        case VSX_WS_PARSER_READING_REQUEST_LINE:
          if (!handle_reading_request_line (parser, &closure, error))
            return VSX_WS_PARSER_RESULT_ERROR;
          break;

        case VSX_WS_PARSER_TERMINATING_REQUEST_LINE:
          if (!handle_terminating_request_line (parser, &closure, error))
            return VSX_WS_PARSER_RESULT_ERROR;
          break;

        case VSX_WS_PARSER_READING_HEADER:
          if (!handle_reading_header (parser, &closure, error))
            return VSX_WS_PARSER_RESULT_ERROR;
          break;

        case VSX_WS_PARSER_TERMINATING_HEADER:
          if (!handle_terminating_header (parser, &closure, error))
            return VSX_WS_PARSER_RESULT_ERROR;
          break;

        case VSX_WS_PARSER_CHECKING_HEADER_CONTINUATION:
          if (!handle_checking_header_continuation (parser, &closure, error))
            return VSX_WS_PARSER_RESULT_ERROR;
          break;

        case VSX_WS_PARSER_DONE:
          *consumed = closure.data - data;
          return VSX_WS_PARSER_RESULT_FINISHED;
        }
    }

  if (parser->state == VSX_WS_PARSER_DONE)
    {
      *consumed = length;
      return VSX_WS_PARSER_RESULT_FINISHED;
    }

  return VSX_WS_PARSER_RESULT_NEED_MORE_DATA;
}

void
vsx_ws_parser_free (VsxWsParser *parser)
{
  g_free (parser);
}

GQuark
vsx_ws_parser_error_quark (void)
{
  return g_quark_from_static_string ("vsx-ws-parser-error");
}
