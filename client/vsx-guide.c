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

#include "vsx-guide.h"

#include "vsx-util.h"

/* Speed the cursor is moved at in mm/s */
#define CURSOR_SPEED 20
/* Speed that a tile moves to jump into place when it is clicked on */
#define JUMP_SPEED 40

#define BIG_TILE_SIZE (VSX_GUIDE_IMAGE_SIZE / 5)
#define SMALL_TILE_SIZE 3

const struct vsx_guide_animation
steal_word_animations[] = {
        /* No animations, just set the positions of the tiles */

        /* TEAR */
        { .thing = 0, .dest_x = 6, .dest_y = 2 },
        { .thing = 1, .dest_x = 9, .dest_y = 2 },
        { .thing = 2, .dest_x = 12, .dest_y = 2 },
        { .thing = 3, .dest_x = 15, .dest_y = 2 },

        /* TEARS */
        { .thing = 4, .dest_x = 1, .dest_y = 11 },
        { .thing = 5, .dest_x = 4, .dest_y = 11 },
        { .thing = 6, .dest_x = 7, .dest_y = 11 },
        { .thing = 7, .dest_x = 10, .dest_y = 11 },
        { .thing = 8, .dest_x = 13, .dest_y = 11 },

        /* RATES */
        { .thing = 9, .dest_x = 9, .dest_y = 17 },
        { .thing = 10, .dest_x = 12, .dest_y = 17 },
        { .thing = 11, .dest_x = 15, .dest_y = 17 },
        { .thing = 12, .dest_x = 18, .dest_y = 17 },
        { .thing = 13, .dest_x = 21, .dest_y = 17 },
};

const struct vsx_guide_animation
move_word_animations[] = {
        /* Zero-length animations to initialise the positions */
        { .thing = 0, .dest_x = 19, .dest_y = 2 },
        { .thing = 1, .dest_x = 3, .dest_y = 12 },
        { .thing = 2, .dest_x = 17, .dest_y = 10 },
        { .thing = 3, .dest_x = 3, .dest_y = 2 },
        { .thing = 4, .dest_x = 9, .dest_y = 6 },
        {
                .thing = VSX_GUIDE_MOVE_CURSOR,
                .dest_x = VSX_GUIDE_IMAGE_SIZE / 2,
                .dest_y = VSX_GUIDE_IMAGE_SIZE / 2,
        },

        /* Move the cursor to the first letter */
        {
                .thing = VSX_GUIDE_MOVE_CURSOR,
                .dest_x = 22, .dest_y = 5,
                .start_after = 0,
                .speed = CURSOR_SPEED,
        },
        /* Move the cursor and the first letter into position */
        {
                .thing = VSX_GUIDE_MOVE_CURSOR,
                .dest_x = 3,
                .dest_y = VSX_GUIDE_IMAGE_SIZE - BIG_TILE_SIZE + 3,
                .start_after = -1,
                .speed = CURSOR_SPEED,
                .click_type = VSX_GUIDE_CLICK_TYPE_DRAG,
        },
        {
                .thing = 0,
                .dest_x = 0,
                .dest_y = VSX_GUIDE_IMAGE_SIZE - BIG_TILE_SIZE,
                .start_after = -2,
                .speed = CURSOR_SPEED,
        },

        /* Move the cursor to the second letter */
        {
                .thing = VSX_GUIDE_MOVE_CURSOR,
                .dest_x = 6, .dest_y = 15,
                .start_after = -1,
                .speed = CURSOR_SPEED,
        },
        /* Make the tile jump into place */
        {
                .thing = 1,
                .dest_x = BIG_TILE_SIZE,
                .dest_y = VSX_GUIDE_IMAGE_SIZE - BIG_TILE_SIZE,
                .start_after = -1,
                .speed = JUMP_SPEED,
        },

        /* Move the cursor to the third letter */
        {
                .thing = VSX_GUIDE_MOVE_CURSOR,
                .dest_x = 20, .dest_y = 13,
                .start_after = -2,
                .speed = CURSOR_SPEED,
                .click_type = VSX_GUIDE_CLICK_TYPE_SHORT,
        },
        /* Third tile jump into place */
        {
                .thing = 2,
                .dest_x = BIG_TILE_SIZE * 2,
                .dest_y = VSX_GUIDE_IMAGE_SIZE - BIG_TILE_SIZE,
                .start_after = -1,
                .speed = JUMP_SPEED,
        },

        /* Move the cursor to the fourth letter */
        {
                .thing = VSX_GUIDE_MOVE_CURSOR,
                .dest_x = 6, .dest_y = 5,
                .start_after = -2,
                .speed = CURSOR_SPEED,
                .click_type = VSX_GUIDE_CLICK_TYPE_SHORT,
        },
        /* Fourth tile jump into place */
        {
                .thing = 3,
                .dest_x = BIG_TILE_SIZE * 3,
                .dest_y = VSX_GUIDE_IMAGE_SIZE - BIG_TILE_SIZE,
                .start_after = -1,
                .speed = JUMP_SPEED,
        },

        /* Move the cursor to the fifth letter */
        {
                .thing = VSX_GUIDE_MOVE_CURSOR,
                .dest_x = 12, .dest_y = 9,
                .start_after = -2,
                .speed = CURSOR_SPEED,
                .click_type = VSX_GUIDE_CLICK_TYPE_SHORT,
        },
        /* Fourth tile jump into place */
        {
                .thing = 4,
                .dest_x = BIG_TILE_SIZE * 4,
                .dest_y = VSX_GUIDE_IMAGE_SIZE - BIG_TILE_SIZE,
                .start_after = -1,
                .speed = JUMP_SPEED,
        },

        /* Move the cursor back to the center */
        {
                .thing = VSX_GUIDE_MOVE_CURSOR,
                .start_after = -2,
                .dest_x = VSX_GUIDE_IMAGE_SIZE / 2,
                .dest_y = VSX_GUIDE_IMAGE_SIZE / 2,
                .speed = CURSOR_SPEED,
                .click_type = VSX_GUIDE_CLICK_TYPE_SHORT,
        },
};

const struct vsx_guide_page
vsx_guide_pages[] = {
        /* What is allowed as a stolen word */
        {
                .text = VSX_TEXT_GUIDE_STEAL_WORD,
                .has_tiles = true,
                .example_word = VSX_TEXT_GUIDE_EXAMPLE_STEAL_WORD,
                .image = "steal-word-guide.mpng",
                .tile_size = SMALL_TILE_SIZE,
                .n_animations = VSX_N_ELEMENTS(steal_word_animations),
                .animations = steal_word_animations,
        },
        /* Explanation of how to move a word */
        {
                .text = VSX_TEXT_GUIDE_MOVE_WORD,
                .has_tiles = true,
                .example_word = VSX_TEXT_GUIDE_EXAMPLE_WORD,
                .tile_size = BIG_TILE_SIZE,
                .show_cursor = true,
                .n_animations = VSX_N_ELEMENTS(move_word_animations),
                .animations = move_word_animations,
        },
};

_Static_assert(VSX_N_ELEMENTS(vsx_guide_pages) ==
               VSX_GUIDE_N_PAGES,
               "Number of element in pages array must match the define");
