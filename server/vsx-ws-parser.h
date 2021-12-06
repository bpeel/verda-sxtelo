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

#include <stdint.h>

#include "vsx-error.h"

typedef struct _VsxWsParser VsxWsParser;

extern struct vsx_error_domain
vsx_ws_parser_error;

typedef enum
{
  VSX_WS_PARSER_ERROR_INVALID,
  VSX_WS_PARSER_ERROR_UNSUPPORTED,
} VsxWsParserError;

typedef enum
{
  VSX_WS_PARSER_RESULT_NEED_MORE_DATA,
  VSX_WS_PARSER_RESULT_FINISHED,
  VSX_WS_PARSER_RESULT_ERROR
} VsxWsParserResult;

VsxWsParser *vsx_ws_parser_new (void);

VsxWsParserResult
vsx_ws_parser_parse_data (VsxWsParser *parser,
                          const uint8_t *data,
                          size_t length,
                          size_t *consumed,
                          struct vsx_error **error);

const uint8_t *
vsx_ws_parser_get_key_hash (VsxWsParser *parser,
                            size_t *key_hash_size);

void vsx_ws_parser_free (VsxWsParser *parser);

#endif /* VSX_WS_PARSER_H */
