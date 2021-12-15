/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2021  Neil Roberts
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

#ifndef VSX_TILE_PAINTER_H
#define VSX_TILE_PAINTER_H

#include "vsx-game-state.h"
#include "vsx-shader-data.h"

struct vsx_tile_painter;

struct vsx_tile_painter *
vsx_tile_painter_new(struct vsx_shader_data *shader_state);

void
vsx_tile_painter_paint(struct vsx_tile_painter *painter,
                       struct vsx_game_state *game_state,
                       int fb_width,
                       int fb_height);

void
vsx_tile_painter_free(struct vsx_tile_painter *painter);

#endif /* VSX_TILE_PAINTER_H */
