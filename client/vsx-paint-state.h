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

#ifndef VSX_PAINT_STATE_H
#define VSX_PAINT_STATE_H

#include <stdbool.h>

struct vsx_paint_state {
        /* Size of the framebuffer */
        int width, height;

        /* DPI of the screen */
        int dpi;

        bool layout_dirty;

        /* The rest of the data is lazily generated on demand */

        /* Position of the board in pixels within the framebuffer.
         * This doesn’t take into account the rotation so they are
         * directly the values that could be used for a scissor.
         * y=0 is the bottom of the framebuffer.
         */
        int board_scissor_x, board_scissor_y;
        int board_scissor_width, board_scissor_height;

        /* true if the board is rotated 90° clockwise */
        bool board_rotated;

        /* Transformation matrix for the board */
        float board_matrix[4];
        /* Board translation */
        float board_translation[2];

        /* Transformation matrix for the button area */
        float button_area_matrix[4];
        /* Button area translation */
        float button_area_translation[2];

        /* Transformation matrix to use pixel coordinates. This also
         * takes into account the rotation.
         */
        float pixel_matrix[4];
        /* Translation to have 0,0 be the topleft */
        float pixel_translation[2];
        /* Size of the screen, taking into account the rotation */
        int pixel_width, pixel_height;

        /* Size in pixels of the button area, taking into account the
         * rotation.
         */
        int button_area_width, button_area_height;
};

void
vsx_paint_state_set_fb_size(struct vsx_paint_state *paint_state,
                            int width, int height);

void
vsx_paint_state_ensure_layout(struct vsx_paint_state *paint_state);

void
vsx_paint_state_offset_pixel_translation(struct vsx_paint_state *paint_state,
                                         float x, float y,
                                         float *translation);

#endif /* VSX_PAINT_STATE_H */
