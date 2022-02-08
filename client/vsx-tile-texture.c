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

#include "vsx-tile-texture.h"

#include <stdlib.h>

const struct vsx_tile_texture_letter *
vsx_tile_texture_find_letter(uint32_t letter)
{
        int min = 0, max = VSX_TILE_TEXTURE_N_LETTERS;

        while (min < max) {
                int mid = (min + max) / 2;
                uint32_t mid_letter = vsx_tile_texture_letters[mid].letter;

                if (mid_letter > letter)
                        max = mid;
                else if (mid_letter == letter)
                        return vsx_tile_texture_letters + mid;
                else
                        min = mid + 1;
        }

        return NULL;
}
