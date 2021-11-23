/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2011, 2015, 2020  Neil Roberts
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

#ifndef __VSX_WS_PARSER_H__
#define __VSX_WS_PARSER_H__

#include <glib.h>

typedef struct _VsxWsParser VsxWsParser;

typedef enum
{
  VSX_WS_PARSER_ERROR_INVALID,
  VSX_WS_PARSER_ERROR_UNSUPPORTED,
  VSX_WS_PARSER_ERROR_CANCELLED
} VsxWsParserError;

typedef struct
{
  gboolean (*request_line_received) (const char *method,
                                     const char *uri,
                                     void *user_data);
  gboolean (*header_received) (const char *field_name,
                               const char *value,
                               void *user_data);
} VsxWsParserVtable;

typedef enum
{
  VSX_WS_PARSER_RESULT_NEED_MORE_DATA,
  VSX_WS_PARSER_RESULT_FINISHED,
  VSX_WS_PARSER_RESULT_ERROR
} VsxWsParserResult;

#define VSX_WS_PARSER_ERROR (vsx_ws_parser_error_quark ())

VsxWsParser *vsx_ws_parser_new (const VsxWsParserVtable *vtable,
                                void *user_data);

VsxWsParserResult
vsx_ws_parser_parse_data (VsxWsParser *parser,
                          const guint8 *data,
                          size_t length,
                          size_t *consumed,
                          GError **error);

void vsx_ws_parser_free (VsxWsParser *parser);

GQuark vsx_ws_parser_error_quark (void);

#endif /* VSX_WS_PARSER_H */
