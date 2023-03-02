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

#include "vsx-paint-state.h"

#include <math.h>
#include <string.h>

#include "vsx-board.h"
#include "vsx-util.h"

/* Minimum size in mm of the button area */
#define BUTTON_AREA_MIN_WIDTH 10

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
calculate_board_transform(struct vsx_paint_state *paint_state)
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

        float min_button_area_size = (BUTTON_AREA_MIN_WIDTH *
                                      paint_state->dpi /
                                      25.4f);
        if (min_button_area_size > paint_state->width / 2)
                min_button_area_size = paint_state->width / 2;

        float button_area_size = large_axis - VSX_BOARD_WIDTH * scale;

        if (button_area_size < min_button_area_size)
                scale = (large_axis - min_button_area_size) / VSX_BOARD_WIDTH;

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

static void
calculate_pixel_transform(struct vsx_paint_state *paint_state)
{
        float *matrix = paint_state->pixel_matrix;
        float *translation = paint_state->pixel_translation;

        if (paint_state->board_rotated) {
                matrix[0] = 0.0f;
                matrix[1] = -2.0f / paint_state->height;
                matrix[2] = -2.0f / paint_state->width;
                matrix[3] = 0.0f;
                translation[0] = 1.0f;
                translation[1] = 1.0f;
                paint_state->pixel_width = paint_state->height;
                paint_state->pixel_height = paint_state->width;
        } else {
                matrix[0] = 2.0f / paint_state->width;
                matrix[1] = 0.0f;
                matrix[2] = 0.0f;
                matrix[3] = -2.0f / paint_state->height;
                translation[0] = -1.0f;
                translation[1] = 1.0f;
                paint_state->pixel_width = paint_state->width;
                paint_state->pixel_height = paint_state->height;
        }
}

void
vsx_paint_state_set_fb_size(struct vsx_paint_state *paint_state,
                             int width,
                             int height)
{
        paint_state->width = MAX(1, width);
        paint_state->height = MAX(1, height);
        paint_state->layout_dirty = true;
}

void
vsx_paint_state_ensure_layout(struct vsx_paint_state *paint_state)
{
        if (!paint_state->layout_dirty)
                return;

        paint_state->layout_dirty = false;

        calculate_board_transform(paint_state);
        calculate_pixel_transform(paint_state);
}

void
vsx_paint_state_offset_pixel_translation(struct vsx_paint_state *paint_state,
                                         float x, float y,
                                         float *translation)
{
        vsx_paint_state_ensure_layout(paint_state);

        if (paint_state->board_rotated) {
                translation[0] = 1.0f - y * 2.0f / paint_state->width;
                translation[1] = 1.0f - x * 2.0f / paint_state->height;
        } else {
                translation[0] = x * 2.0f / paint_state->width - 1.0f;
                translation[1] = 1.0f - y * 2.0f / paint_state->height;
        }
}

void
vsx_paint_state_screen_to_pixel(struct vsx_paint_state *paint_state,
                                int x_in, int y_in,
                                int *x_out, int *y_out)
{
        vsx_paint_state_ensure_layout(paint_state);

        if (paint_state->board_rotated) {
                *x_out = y_in;
                *y_out = paint_state->width - 1 - x_in;
        } else {
                *x_out = x_in;
                *y_out = y_in;
        }
}
