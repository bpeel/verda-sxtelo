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

#ifndef VSX_GUIDE_H
#define VSX_GUIDE_H

#include <limits.h>

#define VSX_GUIDE_EXAMPLE_WORD_LENGTH 5

#define VSX_GUIDE_MOVE_CURSOR INT_MAX

/* Size of the area reserved for the animations or images size in mm */
#define VSX_GUIDE_IMAGE_SIZE 25

/* Size of a tile in mm */
#define VSX_GUIDE_TILE_SIZE (VSX_GUIDE_IMAGE_SIZE / 5)

enum vsx_guide_click_type {
        VSX_GUIDE_CLICK_TYPE_NONE,
        /* Show a short click at the start of the animation */
        VSX_GUIDE_CLICK_TYPE_SHORT,
        /* Show the click icon for the duration of the animation */
        VSX_GUIDE_CLICK_TYPE_DRAG,
};

struct vsx_guide_animation {
        /* Offset of the animation after which this animation should
         * start. Ie, -1 is the animation before this one, etc. Zero
         * means to start immediately.
         */
        int start_after;
        /* The speed of the movement in mm/s, or zero to displace the
         * thing instantaneously.
         */
        int speed;
        /* Thing to move. Either a letter number within the example
         * word, or MOVE_CURSOR to move the cursor.
         */
        int thing;
        /* Where to move to as an offset in mm from the topleft of
         * the image space.
         */
        int dest_x, dest_y;

        enum vsx_guide_click_type click_type;
};

extern const struct vsx_guide_animation
vsx_guide_animations[];

extern const int
vsx_guide_n_animations;

#endif /* VSX_GUIDE_H */
