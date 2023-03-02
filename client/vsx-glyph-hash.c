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

#include "config.h"

#include "vsx-glyph-hash.h"

#include <stdalign.h>

#include "vsx-util.h"
#include "vsx-slab.h"
#include "vsx-list.h"

struct vsx_glyph_hash {
        struct vsx_hash_table table;

        struct vsx_slab_allocator entry_allocator;
};

struct vsx_glyph_hash *
vsx_glyph_hash_new(void)
{
        struct vsx_glyph_hash *hash = vsx_alloc(sizeof *hash);

        vsx_hash_table_init(&hash->table);
        vsx_slab_init(&hash->entry_allocator);

        return hash;
}

struct vsx_glyph_hash_entry *
vsx_glyph_hash_get(struct vsx_glyph_hash *hash,
                   unsigned code,
                   bool *added)
{
        struct vsx_hash_table_entry *hash_entry =
                vsx_hash_table_get(&hash->table, code);

        if (hash_entry) {
                *added = false;
                return vsx_container_of(hash_entry,
                                        struct vsx_glyph_hash_entry,
                                        hash_entry);
        }

        struct vsx_glyph_hash_entry *entry =
                vsx_slab_allocate(&hash->entry_allocator,
                                  sizeof (struct vsx_glyph_hash_entry),
                                  alignof (struct vsx_glyph_hash_entry));

        entry->hash_entry.id = code;

        vsx_hash_table_add(&hash->table, &entry->hash_entry);

        *added = true;

        return entry;
}

void
vsx_glyph_hash_free(struct vsx_glyph_hash *hash)
{
        vsx_hash_table_destroy(&hash->table);
        vsx_slab_destroy(&hash->entry_allocator);
        vsx_free(hash);
}
