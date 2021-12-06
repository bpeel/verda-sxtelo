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

#include <openssl/evp.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>
#include <stdbool.h>

#define VSX_WS_PARSER_MAX_LINE_LENGTH 512

struct _VsxWsParser
{
  unsigned int buf_len;

  uint8_t buf[VSX_WS_PARSER_MAX_LINE_LENGTH];

  enum
  {
    VSX_WS_PARSER_READING_REQUEST_LINE,
    VSX_WS_PARSER_TERMINATING_REQUEST_LINE,
    VSX_WS_PARSER_READING_HEADER,
    VSX_WS_PARSER_TERMINATING_HEADER,
    VSX_WS_PARSER_CHECKING_HEADER_CONTINUATION,
    VSX_WS_PARSER_DONE
  } state;

  uint8_t key_hash[EVP_MAX_MD_SIZE];
  unsigned int key_hash_length;

  EVP_MD_CTX *key_hash_ctx;
};

struct vsx_error_domain
vsx_ws_parser_error;

static const char
ws_sec_key_guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

VsxWsParser *
vsx_ws_parser_new (void)
{
  VsxWsParser *parser = vsx_alloc (sizeof *parser);

  parser->buf_len = 0;
  parser->state = VSX_WS_PARSER_READING_REQUEST_LINE;
  parser->key_hash_ctx = NULL;

  return parser;
}

static bool
check_http_version (const uint8_t *data,
                    unsigned int length,
                    struct vsx_error **error)
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

  return true;

bad:
  vsx_set_error (error,
                 &vsx_ws_parser_error,
                 VSX_WS_PARSER_ERROR_UNSUPPORTED,
                 "Unsupported HTTP version");
  return false;
}

static bool
add_bytes_to_buffer (VsxWsParser *parser,
                     const uint8_t *data,
                     unsigned int length,
                     struct vsx_error **error)
{
  if (parser->buf_len + length > VSX_WS_PARSER_MAX_LINE_LENGTH)
    {
      vsx_set_error (error,
                     &vsx_ws_parser_error,
                     VSX_WS_PARSER_ERROR_UNSUPPORTED,
                     "Unsupported line length in HTTP request");
      return false;
    }
  else
    {
      memcpy (parser->buf + parser->buf_len, data, length);
      parser->buf_len += length;

      return true;
    }
}

static bool
process_request_line (VsxWsParser *parser,
                      uint8_t *data,
                      unsigned int length,
                      struct vsx_error **error)
{
  uint8_t *method_end = memchr (data, ' ', length);

  if (method_end == NULL)
    {
      vsx_set_error (error,
                     &vsx_ws_parser_error,
                     VSX_WS_PARSER_ERROR_INVALID,
                     "Invalid HTTP request received");
      return false;
    }

  /* Replace the space with a zero terminator so we can reuse
   * the buffer to pass to the callback
   */
  *method_end = '\0';

  length -= method_end - data + 1;
  data = method_end + 1;

  uint8_t *uri_end = memchr (data, ' ', length);

  if (uri_end == NULL)
    {
      vsx_set_error (error,
                     &vsx_ws_parser_error,
                     VSX_WS_PARSER_ERROR_INVALID,
                     "Invalid HTTP request received");
      return false;
    }

  *uri_end = '\0';

  length -= uri_end - data + 1;
  data = uri_end + 1;

  if (!check_http_version (data, length, error))
    return false;

  return true;
}

static bool
process_header (VsxWsParser *parser, struct vsx_error **error)
{
  uint8_t *data = parser->buf;
  unsigned int length = parser->buf_len;
  const char *field_name = (char *) data;
  uint8_t *field_name_end;

  field_name_end = memchr (data, ':', length);

  if (field_name_end == NULL)
    {
      vsx_set_error (error,
                     &vsx_ws_parser_error,
                     VSX_WS_PARSER_ERROR_INVALID,
                     "Invalid HTTP request received");
      return false;
    }

  static const char key_header[] = "sec-websocket-key:";

  /* Ignore any headers apart from the key header */
  if (g_ascii_strncasecmp (field_name, key_header, (sizeof key_header) - 1))
    return true;

  if (parser->key_hash_ctx != NULL)
    {
      vsx_set_error (error,
                     &vsx_ws_parser_error,
                     VSX_WS_PARSER_ERROR_INVALID,
                     "Client sent a WebSocket header with multiple "
                     "Sec-WebSocket-Key headers");
      return false;
    }

  length -= field_name_end - data + 1;
  data = field_name_end + 1;

  /* Skip leading spaces */
  while (length > 0 && *data == ' ')
    {
      length--;
      data++;
    }

  parser->key_hash_ctx = EVP_MD_CTX_new ();
  EVP_DigestInit_ex (parser->key_hash_ctx, EVP_sha1 (), NULL);
  EVP_DigestUpdate (parser->key_hash_ctx, data, length);

  return true;
}

typedef struct
{
  const uint8_t *data;
  unsigned int length;
} VsxWsParserClosure;

static bool
handle_reading_request_line (VsxWsParser *parser,
                             VsxWsParserClosure *c,
                             struct vsx_error **error)
{
  const uint8_t *terminator;

  /* Could the data contain a terminator? */
  if ((terminator = memchr (c->data, '\r', c->length)))
    {
      /* Add the data up to the potential terminator */
      if (!add_bytes_to_buffer (parser, c->data, terminator - c->data, error))
        return false;

      /* Consume those bytes */
      c->length -= terminator - c->data + 1;
      c->data = terminator + 1;

      parser->state = VSX_WS_PARSER_TERMINATING_REQUEST_LINE;
    }
  else
    {
      /* Add and consume all of the data */
      if (!add_bytes_to_buffer (parser, c->data, c->length, error))
        return false;

      c->length = 0;
    }

  return true;
}

static bool
handle_terminating_request_line (VsxWsParser *parser,
                                 VsxWsParserClosure *c,
                                 struct vsx_error **error)
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
            return false;

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
      uint8_t r = '\r';
      /* Add the \r that we ignored when switching to this
       * state and then switch back to reading the request
       * line without consuming the char
       */
      if (!add_bytes_to_buffer (parser, &r, 1, error))
        return false;
      parser->state = VSX_WS_PARSER_READING_REQUEST_LINE;
    }

  return true;
}

static bool
handle_reading_header (VsxWsParser *parser,
                       VsxWsParserClosure *c,
                       struct vsx_error **error)
{
  const uint8_t *terminator;

  /* Could the data contain a terminator? */
  if ((terminator = memchr (c->data, '\r', c->length)))
    {
      /* Add the data up to the potential terminator */
      if (!add_bytes_to_buffer (parser, c->data, terminator - c->data, error))
        return false;

      /* Consume those bytes */
      c->length -= terminator - c->data + 1;
      c->data = terminator + 1;

      parser->state = VSX_WS_PARSER_TERMINATING_HEADER;
    }
  else
    {
      /* Add and consume all of the data */
      if (!add_bytes_to_buffer (parser, c->data, c->length, error))
        return false;

      c->length = 0;
    }

  return true;
}

static bool
finish_key_hash (VsxWsParser *parser,
                 struct vsx_error **error)
{
  if (parser->key_hash_ctx == NULL)
    {
      vsx_set_error (error,
                     &vsx_ws_parser_error,
                     VSX_WS_PARSER_ERROR_INVALID,
                     "Client sent a WebSocket header without a "
                     "Sec-WebSocket-Key header");
      return false;
    }

  EVP_DigestUpdate (parser->key_hash_ctx,
                    ws_sec_key_guid,
                    sizeof ws_sec_key_guid - 1);
  EVP_DigestFinal (parser->key_hash_ctx,
                   parser->key_hash,
                   &parser->key_hash_length);

  return true;
}

static bool
handle_terminating_header (VsxWsParser *parser,
                           VsxWsParserClosure *c,
                           struct vsx_error **error)
{
  /* Do we have the \n needed to complete the terminator? */
  if (c->data[0] == '\n')
    {
      /* If the header is empty then this marks the end of the
       * headers
       */
      if (parser->buf_len == 0)
        {
          if (!finish_key_hash (parser, error))
            return false;

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
      uint8_t r = '\r';
      /* Add the \r that we ignored when switching to this
       * state and then switch back to reading the header
       * without consuming the char
       */
      if (!add_bytes_to_buffer (parser, &r, 1, error))
        return false;
      parser->state = VSX_WS_PARSER_READING_HEADER;
    }

  return true;
}

static bool
handle_checking_header_continuation (VsxWsParser *parser,
                                     VsxWsParserClosure *c,
                                     struct vsx_error **error)
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
        return false;

      parser->buf_len = 0;
      parser->state = VSX_WS_PARSER_READING_HEADER;
    }

  return true;
}

VsxWsParserResult
vsx_ws_parser_parse_data (VsxWsParser *parser,
                          const uint8_t *data,
                          size_t length,
                          size_t *consumed,
                          struct vsx_error **error)
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

const uint8_t *
vsx_ws_parser_get_key_hash (VsxWsParser *parser,
                            size_t *key_hash_size)
{
  *key_hash_size = parser->key_hash_length;
  return parser->key_hash;
}

void
vsx_ws_parser_free (VsxWsParser *parser)
{
  if (parser->key_hash_ctx)
    EVP_MD_CTX_free (parser->key_hash_ctx);

  vsx_free (parser);
}
