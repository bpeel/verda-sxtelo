/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2022  Neil Roberts
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

#include "vsx-board.h"

#include "vsx-util.h"

const struct vsx_board_player_space
vsx_board_player_spaces[] = {
        {
                .center_x = (VSX_BOARD_MIDDLE_X +
                             VSX_BOARD_MIDDLE_WIDTH / 2),
                .center_y = VSX_BOARD_MIDDLE_HEIGHT / 2,
        },
        {
                .center_x = (VSX_BOARD_MIDDLE_X +
                             VSX_BOARD_MIDDLE_WIDTH / 2),
                .center_y = VSX_BOARD_HEIGHT - VSX_BOARD_MIDDLE_HEIGHT / 2,
        },
        {
                .center_x = VSX_BOARD_SIDE_WIDTH / 2,
                .center_y = VSX_BOARD_SIDE_HEIGHT / 2,
        },
        {
                .center_x = VSX_BOARD_WIDTH - VSX_BOARD_SIDE_WIDTH / 2,
                .center_y = VSX_BOARD_SIDE_HEIGHT / 2,
        },
        {
                .center_x = VSX_BOARD_SIDE_WIDTH / 2,
                .center_y = VSX_BOARD_HEIGHT - VSX_BOARD_SIDE_HEIGHT / 2,
        },
        {
                .center_x = VSX_BOARD_WIDTH - VSX_BOARD_SIDE_WIDTH / 2,
                .center_y = VSX_BOARD_HEIGHT - VSX_BOARD_SIDE_HEIGHT / 2,
        },
};

_Static_assert(VSX_N_ELEMENTS(vsx_board_player_spaces) ==
               VSX_BOARD_N_PLAYER_SPACES,
               "The player spaces array needs to have the right number of "
               "entries to match the constant.");
