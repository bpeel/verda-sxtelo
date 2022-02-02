/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2021, 2022  Neil Roberts
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

#ifndef VSX_BOARD_H
#define VSX_BOARD_H

#define VSX_BOARD_WIDTH 600
#define VSX_BOARD_HEIGHT 360

#define VSX_BOARD_SIDE_HEIGHT 170
#define VSX_BOARD_SIDE_WIDTH 90
#define VSX_BOARD_MIDDLE_HEIGHT VSX_BOARD_SIDE_WIDTH
#define VSX_BOARD_MIDDLE_WIDTH VSX_BOARD_SIDE_HEIGHT
#define VSX_BOARD_CORNER_SIZE 20
#define VSX_BOARD_MIDDLE_X (VSX_BOARD_WIDTH / 2 -       \
                            VSX_BOARD_MIDDLE_WIDTH / 2)

#define VSX_BOARD_N_PLAYER_SPACES 6

#endif /* VSX_BOARD_H */
