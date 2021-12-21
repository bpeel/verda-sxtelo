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
#include <string.h>

#include "vsx-painter.h"
#include "vsx-board-painter.h"
#include "vsx-tile-painter.h"
#include "vsx-gl.h"
#include "vsx-board.h"

static const struct vsx_painter * const
painters[] = {
        &vsx_board_painter,
        &vsx_tile_painter,
};

#define N_PAINTERS VSX_N_ELEMENTS(painters)

struct painter_data {
        void *data;
        struct vsx_listener listener;
        struct vsx_game_painter *game_painter;
};

struct vsx_game_painter {
        struct vsx_painter_toolbox toolbox;
        bool shader_data_inited;

        struct vsx_paint_state paint_state;

        bool viewport_dirty;

        struct painter_data painters[N_PAINTERS];

        struct vsx_signal redraw_needed_signal;
};

static void
redraw_needed_cb(struct vsx_listener *listener,
                 void *signal_data)
{
        struct painter_data *painter_data =
                vsx_container_of(listener, struct painter_data, listener);
        struct vsx_game_painter *painter = painter_data->game_painter;

        vsx_signal_emit(&painter->redraw_needed_signal, painter);
}

static void
init_redraw_needed_listener(struct vsx_game_painter *painter,
                            struct painter_data *painter_data,
                            const struct vsx_painter *callbacks)
{
        struct vsx_signal *signal =
                callbacks->get_redraw_needed_signal_cb(painter_data->data);

        painter_data->listener.notify = redraw_needed_cb;

        vsx_signal_add(signal, &painter_data->listener);
}

static void
init_painters(struct vsx_game_painter *painter)
{
        for (unsigned i = 0; i < N_PAINTERS; i++) {
                struct painter_data *painter_data = painter->painters + i;
                const struct vsx_painter *callbacks = painters[i];

                painter_data->data = callbacks->create_cb(&painter->toolbox);

                painter_data->game_painter = painter;

                if (callbacks->get_redraw_needed_signal_cb) {
                        init_redraw_needed_listener(painter,
                                                    painter_data,
                                                    callbacks);
                }
        }
}

static bool
init_toolbox(struct vsx_game_painter *painter,
             struct vsx_asset_manager *asset_manager,
             struct vsx_error **error)
{
        struct vsx_painter_toolbox *toolbox = &painter->toolbox;

        if (!vsx_shader_data_init(&toolbox->shader_data,
                                  asset_manager,
                                  error))
                return false;

        painter->shader_data_inited = true;

        toolbox->image_loader = vsx_image_loader_new(asset_manager);

        return true;
}

static void
destroy_toolbox(struct vsx_game_painter *painter)
{
        struct vsx_painter_toolbox *toolbox = &painter->toolbox;

        if (toolbox->image_loader)
                vsx_image_loader_free(toolbox->image_loader);

        if (painter->shader_data_inited)
                vsx_shader_data_destroy(&toolbox->shader_data);
}

struct vsx_game_painter *
vsx_game_painter_new(struct vsx_asset_manager *asset_manager,
                     struct vsx_error **error)
{
        struct vsx_game_painter *painter = vsx_calloc(sizeof *painter);

        painter->paint_state.width = 1;
        painter->paint_state.height = 1;
        painter->viewport_dirty = true;

        vsx_signal_init(&painter->redraw_needed_signal);

        if (!init_toolbox(painter, asset_manager, error))
                goto error;

        init_painters(painter);

        return painter;

error:
        vsx_game_painter_free(painter);
        return NULL;
}

static void
fit_board_normal(struct vsx_paint_state *paint_state,
                 float scale)
{
        paint_state->board_matrix[0] =
                scale * 2.0f / paint_state->width;
        paint_state->board_matrix[1] = 0.0f;
        paint_state->board_matrix[2] = 0.0f;
        paint_state->board_matrix[3] =
                -scale * 2.0f / paint_state->height;
        paint_state->board_translation[0] = -1.0f;
        paint_state->board_translation[1] =
                -VSX_BOARD_HEIGHT / 2.0f * paint_state->board_matrix[3];
}

static void
fit_board_rotated(struct vsx_paint_state *paint_state,
                 float scale)
{
        paint_state->board_matrix[0] = 0.0f;
        paint_state->board_matrix[1] =
                -scale * 2.0f / paint_state->height;
        paint_state->board_matrix[2] =
                -scale * 2.0f / paint_state->width;
        paint_state->board_matrix[3] = 0.0f;
        paint_state->board_translation[0] =
                -VSX_BOARD_HEIGHT / 2.0f * paint_state->board_matrix[2];
        paint_state->board_translation[1] = 1.0f;
}

static void
calculate_transform(struct vsx_paint_state *paint_state)
{
        int large_axis, small_axis;

        if (paint_state->width > paint_state->height) {
                large_axis = paint_state->width;
                small_axis = paint_state->height;
                paint_state->board_rotated = false;
        } else {
                large_axis = paint_state->height;
                small_axis = paint_state->width;
                paint_state->board_rotated = true;
        }

        /* We want to know if the (possibly rotated) framebuffer
         * width/height ratio is greater than the board width/height
         * ratio. Otherwise we will fit the board so that the width
         * fills the screen instead of the height.
         *
         * (a/b > c/d) == (a*d/b*d > c*b/b*d) == (a*d > c*b)
         */
        bool fit_small = (large_axis * VSX_BOARD_HEIGHT >
                          VSX_BOARD_WIDTH * small_axis);

        float scale = (fit_small ?
                       small_axis / (float) VSX_BOARD_HEIGHT :
                       large_axis / (float) VSX_BOARD_WIDTH);

        if (paint_state->board_rotated)
                fit_board_rotated(paint_state, scale);
        else
                fit_board_normal(paint_state, scale);


        float x1 = ((paint_state->board_translation[0] + 1.0f) *
                    paint_state->width / 2.0f);
        float y1 = ((paint_state->board_translation[1] + 1.0f) *
                    paint_state->height / 2.0f);
        float x2 = ((VSX_BOARD_WIDTH * paint_state->board_matrix[0] +
                     VSX_BOARD_HEIGHT * paint_state->board_matrix[2] +
                     paint_state->board_translation[0] + 1.0f) *
                    paint_state->width / 2.0f);
        float y2 = ((VSX_BOARD_WIDTH * paint_state->board_matrix[1] +
                     VSX_BOARD_HEIGHT * paint_state->board_matrix[3] +
                     paint_state->board_translation[1] + 1.0f) *
                    paint_state->height / 2.0f);
        paint_state->board_scissor_x = roundf(fminf(x1, x2));
        paint_state->board_scissor_y = roundf(fminf(y1, y2));
        paint_state->board_scissor_width = roundf(fabsf(x2 - x1));
        paint_state->board_scissor_height = roundf(fabsf(y2 - y1));
}

void
vsx_game_painter_set_fb_size(struct vsx_game_painter *painter,
                             int width,
                             int height)
{
        painter->paint_state.width = MAX(1, width);
        painter->paint_state.height = MAX(1, height);
        painter->viewport_dirty = true;
}

void
vsx_game_painter_paint(struct vsx_game_painter *painter,
                       struct vsx_game_state *game_state)
{
        if (painter->viewport_dirty) {
                vsx_gl.glViewport(0, 0,
                                  painter->paint_state.width,
                                  painter->paint_state.height);
                calculate_transform(&painter->paint_state);

                painter->viewport_dirty = false;
        }

        vsx_gl.glClear(GL_COLOR_BUFFER_BIT);

        for (unsigned i = 0; i < N_PAINTERS; i++) {
                if (painters[i]->paint_cb == NULL)
                        continue;

                painters[i]->paint_cb(painter->painters[i].data,
                                      game_state,
                                      &painter->paint_state);
        }
}

struct vsx_signal *
vsx_game_painter_get_redraw_needed_signal(struct vsx_game_painter *painter)
{
        return &painter->redraw_needed_signal;
}

static void
free_painters(struct vsx_game_painter *painter)
{
        for (unsigned i = 0; i < N_PAINTERS; i++) {
                void *data = painter->painters[i].data;

                if (data == NULL)
                        continue;

                painters[i]->free_cb(data);
        }
}

void
vsx_game_painter_free(struct vsx_game_painter *painter)
{
        free_painters(painter);

        destroy_toolbox(painter);

        vsx_free(painter);
}
