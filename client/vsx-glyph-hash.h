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

#ifndef VSX_GLYPH_HASH_H
#define VSX_GLYPH_HASH_H

#include <stdint.h>
#include <stdbool.h>

#include "vsx-hash-table.h"

struct vsx_glyph_hash;

struct vsx_glyph_hash_entry {
        struct vsx_hash_table_entry hash_entry;

        int left, top;
        long x_advance;
        /* This will be zero if the glyph shouldn’t be renderered
         * (like the space glyph)
         */
        unsigned tex_num;
        uint16_t s1, t1;
        uint16_t s2, t2;
        uint16_t width, height;
};

struct vsx_glyph_hash *
vsx_glyph_hash_new(void);

struct vsx_glyph_hash_entry *
vsx_glyph_hash_get(struct vsx_glyph_hash *hash,
                   unsigned code,
                   bool *added);

void
vsx_glyph_hash_free(struct vsx_glyph_hash *hash);

#endif /* VSX_GLYPH_HASH_H */
