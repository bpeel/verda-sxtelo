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

#ifndef __VSX_STRING_RESPONSE_H__
#define __VSX_STRING_RESPONSE_H__

#include "vsx-response.h"

G_BEGIN_DECLS

typedef enum
{
  VSX_STRING_RESPONSE_BAD_REQUEST,
  VSX_STRING_RESPONSE_UNSUPPORTED_REQUEST,
  VSX_STRING_RESPONSE_NOT_FOUND,
  VSX_STRING_RESPONSE_REQUEST_TIMEOUT,
  VSX_STRING_RESPONSE_PREFLIGHT_POST_OK,
  VSX_STRING_RESPONSE_OK
} VsxStringResponseType;

typedef struct
{
  VsxResponse parent;

  VsxStringResponseType type;

  unsigned int output_pos;
} VsxStringResponse;

VsxResponse *
vsx_string_response_new (VsxStringResponseType type);

G_END_DECLS

#endif /* __VSX_STRING_RESPONSE_H__ */

