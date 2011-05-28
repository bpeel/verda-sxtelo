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

#ifndef __GML_HTTP_PARSER_H__
#define __GML_HTTP_PARSER_H__

#include <glib.h>

G_BEGIN_DECLS

#define GML_HTTP_PARSER_MAX_LINE_LENGTH 512

typedef enum
{
  GML_HTTP_PARSER_ERROR_INVALID,
  GML_HTTP_PARSER_ERROR_UNSUPPORTED,
  GML_HTTP_PARSER_ERROR_CANCELLED
} GmlHttpParserError;

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
} GmlHttpParserVtable;

typedef struct
{
  unsigned int buf_len;
  guint8 buf[GML_HTTP_PARSER_MAX_LINE_LENGTH];

  enum
  {
    GML_HTTP_PARSER_READING_REQUEST_LINE,
    GML_HTTP_PARSER_TERMINATING_REQUEST_LINE,
    GML_HTTP_PARSER_READING_HEADER,
    GML_HTTP_PARSER_TERMINATING_HEADER,
    GML_HTTP_PARSER_CHECKING_HEADER_CONTINUATION,
    GML_HTTP_PARSER_READING_DATA_WITH_LENGTH,
    GML_HTTP_PARSER_READING_CHUNK_LENGTH,
    GML_HTTP_PARSER_TERMINATING_CHUNK_LENGTH,
    GML_HTTP_PARSER_IGNORING_CHUNK_EXTENSION,
    GML_HTTP_PARSER_TERMINATING_CHUNK_EXTENSION,
    GML_HTTP_PARSER_IGNORING_CHUNK_TRAILER,
    GML_HTTP_PARSER_TERMINATING_CHUNK_TRAILER,
    GML_HTTP_PARSER_READING_CHUNK,
    GML_HTTP_PARSER_READING_CHUNK_TERMINATOR1,
    GML_HTTP_PARSER_READING_CHUNK_TERMINATOR2
  } state;

  const GmlHttpParserVtable *vtable;
  void *user_data;

  enum
  {
    GML_HTTP_PARSER_TRANSFER_NONE,
    GML_HTTP_PARSER_TRANSFER_CONTENT_LENGTH,
    GML_HTTP_PARSER_TRANSFER_CHUNKED
  } transfer_encoding;

  unsigned int content_length;
} GmlHttpParser;

#define GML_HTTP_PARSER_ERROR (gml_http_parser_error_quark ())

void
gml_http_parser_init (GmlHttpParser *parser,
                      const GmlHttpParserVtable *vtable,
                      void *user_data);

gboolean
gml_http_parser_parse_data (GmlHttpParser *parser,
                            const guint8 *data,
                            unsigned int length,
                            GError **error);

GQuark
gml_http_parser_error_quark (void);

G_END_DECLS

#endif /* __GML_HTTP_PARSER_H__ */
