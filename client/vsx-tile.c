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

#include "config.h"

#include "vsx-tile-private.h"

int
vsx_tile_get_number (const struct vsx_tile *tile)
{
  return tile->num;
}

int
vsx_tile_get_x (const struct vsx_tile *tile)
{
  return tile->x;
}

int
vsx_tile_get_y (const struct vsx_tile *tile)
{
  return tile->y;
}

uint32_t
vsx_tile_get_letter (const struct vsx_tile *tile)
{
  return tile->letter;
}
