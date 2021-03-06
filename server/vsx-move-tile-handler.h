/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2013  Neil Roberts
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

#ifndef __VSX_MOVE_TILE_HANDLER_H__
#define __VSX_MOVE_TILE_HANDLER_H__

#include <glib.h>

#include "vsx-request-handler.h"
#include "vsx-person.h"

G_BEGIN_DECLS

typedef struct
{
  VsxRequestHandler parent;

  VsxPerson *person;
  int tile_num;
  int x, y;

  VsxResponse *response;
} VsxMoveTileHandler;

VsxRequestHandler *
vsx_move_tile_handler_new (void);

G_END_DECLS

#endif /* __VSX_MOVE_TILE_HANDLER_H__ */
