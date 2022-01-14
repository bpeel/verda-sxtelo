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

#include "vsx-util.h"

struct vsx_glyph_hash {
        int n_entries;
        int size;
        /* All entries in an array */
        struct vsx_glyph_hash_entry *entries;
        /* Lookup of indices into entries array by hash index */
        unsigned *lookup;
};

static unsigned
get_hash_index(struct vsx_glyph_hash *hash,
               unsigned code)
{
        return code % hash->size;
}

static void
link_entry(struct vsx_glyph_hash *hash,
           int hash_index,
           int entry_index)
{
        struct vsx_glyph_hash_entry *entry = hash->entries + entry_index;
        entry->next = hash->lookup[hash_index];
        hash->lookup[hash_index] = entry_index + 1;
}

static void
grow_hash(struct vsx_glyph_hash *hash)
{
        hash->size *= 2;
        hash->entries = vsx_realloc(hash->entries,
                                    hash->size * sizeof hash->entries[0]);
        vsx_free(hash->lookup);
        hash->lookup = vsx_calloc(hash->size * sizeof hash->lookup[0]);

        for (int i = 0; i < hash->n_entries; i++) {
                struct vsx_glyph_hash_entry *entry = hash->entries + i;
                link_entry(hash, get_hash_index(hash, entry->code), i);
        }
}

struct vsx_glyph_hash *
vsx_glyph_hash_new(void)
{
        struct vsx_glyph_hash *hash = vsx_alloc(sizeof *hash);

        hash->n_entries = 0;
        hash->size = 1;
        hash->entries = vsx_alloc(hash->size * sizeof hash->entries[0]);
        hash->lookup = vsx_calloc(hash->size * sizeof hash->lookup[0]);

        return hash;
}

struct vsx_glyph_hash_entry *
vsx_glyph_hash_get(struct vsx_glyph_hash *hash,
                   unsigned code,
                   bool *added)
{
        unsigned hash_index = get_hash_index(hash, code);
        struct vsx_glyph_hash_entry *entry;

        for (int entry_num = hash->lookup[hash_index];
             entry_num;
             entry_num = entry->next) {
                entry = hash->entries + entry_num - 1;

                if (entry->code == code) {
                        *added = false;
                        return entry;
                }
        }

        if (hash->n_entries > hash->size * 3 / 4) {
                grow_hash(hash);
                hash_index = get_hash_index(hash, code);
        }

        int entry_index = hash->n_entries++;
        entry = hash->entries + entry_index;

        entry->code = code;

        link_entry(hash, hash_index, entry_index);

        *added = true;

        return entry;
}

void
vsx_glyph_hash_free(struct vsx_glyph_hash *hash)
{
        vsx_free(hash->entries);
        vsx_free(hash->lookup);
        vsx_free(hash);
}
