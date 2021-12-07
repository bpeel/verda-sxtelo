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

#ifndef VSX_TILE_DATA_H
#define VSX_TILE_DATA_H

#include "vsx-tile.h"

#define VSX_TILE_DATA_N_ROOMS 2
#define VSX_TILE_DATA_N_TILES 122

typedef struct
{
  const char *language_code;
  const char *letters;
} VsxTileData;

extern const VsxTileData
vsx_tile_data[VSX_TILE_DATA_N_ROOMS];

#endif /* VSX_TILE_DATA_H */
