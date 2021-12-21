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

struct vsx_paint_state {
        /* Size of the framebuffer */
        int width, height;

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
};

#endif /* VSX_PAINT_STATE_H */
