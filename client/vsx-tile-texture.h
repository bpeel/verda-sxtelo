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

#ifndef VSX_TILE_TEXTURE_H
#define VSX_TILE_TEXTURE_H

#include <stdint.h>

#define VSX_TILE_TEXTURE_N_LETTERS 80

struct vsx_tile_texture_letter {
        uint32_t letter;
        uint16_t s1, t1;
        uint16_t s2, t2;
};

extern const struct vsx_tile_texture_letter
vsx_tile_texture_letters[VSX_TILE_TEXTURE_N_LETTERS];

const struct vsx_tile_texture_letter *
vsx_tile_texture_find_letter(uint32_t letter);

#endif /* VSX_TILE_TEXTURE_H */
