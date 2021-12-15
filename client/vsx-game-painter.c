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

#include "config.h"

#include "vsx-game-painter.h"

#include <stdbool.h>
#include <math.h>

#include "vsx-painter-toolbox.h"
#include "vsx-tile-painter.h"
#include "vsx-gl.h"

struct vsx_game_painter {
        struct vsx_painter_toolbox toolbox;
        bool shader_data_inited;

        struct vsx_tile_painter *tile_painter;
};

struct vsx_game_painter *
vsx_game_painter_new(struct vsx_asset_manager *asset_manager,
                     struct vsx_error **error)
{
        struct vsx_game_painter *painter = vsx_calloc(sizeof *painter);

        if (!vsx_shader_data_init(&painter->toolbox.shader_data,
                                  asset_manager,
                                  error))
                goto error;

        painter->shader_data_inited = true;

        painter->tile_painter = vsx_tile_painter_new(&painter->toolbox);

        return painter;

error:
        vsx_game_painter_free(painter);
        return NULL;
}

void
vsx_game_painter_paint(struct vsx_game_painter *painter,
                       struct vsx_game_state *game_state,
                       int width,
                       int height)
{
        vsx_gl.glViewport(0, 0, width, height);

        vsx_gl.glClear(GL_COLOR_BUFFER_BIT);

        vsx_tile_painter_paint(painter->tile_painter,
                               game_state,
                               width,
                               height);
}

void
vsx_game_painter_free(struct vsx_game_painter *painter)
{
        if (painter->tile_painter)
                vsx_tile_painter_free(painter->tile_painter);

        if (painter->shader_data_inited)
                vsx_shader_data_destroy(&painter->toolbox.shader_data);

        vsx_free(painter);
}
