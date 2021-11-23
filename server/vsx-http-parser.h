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

#ifndef __VSX_HTTP_PARSER_H__
#define __VSX_HTTP_PARSER_H__

#include <glib.h>

G_BEGIN_DECLS

#define VSX_HTTP_PARSER_MAX_LINE_LENGTH 512

typedef enum
{
  VSX_HTTP_PARSER_ERROR_INVALID,
  VSX_HTTP_PARSER_ERROR_UNSUPPORTED,
  VSX_HTTP_PARSER_ERROR_CANCELLED
} VsxHttpParserError;

typedef struct
{
  gboolean (* request_line_received) (const char *method,
                                      const char *uri,
                                      void *user_data);
  gboolean (* header_received) (const char *field_name,
                                const char *value,
                                void *user_data);
  gboolean (* data_received) (const guint8 *data,
                              unsigned int length,
                              void *user_data);
  gboolean (* request_finished) (void *user_data);
} VsxHttpParserVtable;

typedef struct
{
  unsigned int buf_len;
  guint8 buf[VSX_HTTP_PARSER_MAX_LINE_LENGTH];

  enum
  {
    VSX_HTTP_PARSER_READING_REQUEST_LINE,
    VSX_HTTP_PARSER_TERMINATING_REQUEST_LINE,
    VSX_HTTP_PARSER_READING_HEADER,
    VSX_HTTP_PARSER_TERMINATING_HEADER,
    VSX_HTTP_PARSER_CHECKING_HEADER_CONTINUATION,
    VSX_HTTP_PARSER_READING_DATA_WITH_LENGTH,
    VSX_HTTP_PARSER_READING_CHUNK_LENGTH,
    VSX_HTTP_PARSER_TERMINATING_CHUNK_LENGTH,
    VSX_HTTP_PARSER_IGNORING_CHUNK_EXTENSION,
    VSX_HTTP_PARSER_TERMINATING_CHUNK_EXTENSION,
    VSX_HTTP_PARSER_IGNORING_CHUNK_TRAILER,
    VSX_HTTP_PARSER_TERMINATING_CHUNK_TRAILER,
    VSX_HTTP_PARSER_READING_CHUNK,
    VSX_HTTP_PARSER_READING_CHUNK_TERMINATOR1,
    VSX_HTTP_PARSER_READING_CHUNK_TERMINATOR2
  } state;

  const VsxHttpParserVtable *vtable;
  void *user_data;

  enum
  {
    VSX_HTTP_PARSER_TRANSFER_NONE,
    VSX_HTTP_PARSER_TRANSFER_CONTENT_LENGTH,
    VSX_HTTP_PARSER_TRANSFER_CHUNKED
  } transfer_encoding;

  unsigned int content_length;
} VsxHttpParser;

#define VSX_HTTP_PARSER_ERROR (vsx_http_parser_error_quark ())

void
vsx_http_parser_init (VsxHttpParser *parser,
                      const VsxHttpParserVtable *vtable,
                      void *user_data);

gboolean
vsx_http_parser_parse_data (VsxHttpParser *parser,
                            const guint8 *data,
                            unsigned int length,
                            GError **error);

gboolean
vsx_http_parser_parse_eof (VsxHttpParser *parser,
                           GError **error);

GQuark
vsx_http_parser_error_quark (void);

G_END_DECLS

#endif /* __VSX_HTTP_PARSER_H__ */
