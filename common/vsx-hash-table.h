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

#ifndef VSX_HASH_TABLE_H
#define VSX_HASH_TABLE_H

#include <stdint.h>

struct vsx_hash_table_entry {
        uint64_t id;
        struct vsx_hash_table_entry *next;
};

struct vsx_hash_table {
        int n_entries;
        int table_size;
        struct vsx_hash_table_entry **entries;
};

void
vsx_hash_table_init(struct vsx_hash_table *hash_table);

struct vsx_hash_table_entry *
vsx_hash_table_get(struct vsx_hash_table *hash_table,
                   uint64_t key);

void
vsx_hash_table_add(struct vsx_hash_table *hash_table,
                   struct vsx_hash_table_entry *entry);

void
vsx_hash_table_remove(struct vsx_hash_table *hash_table,
                      struct vsx_hash_table_entry *entry);

void
vsx_hash_table_destroy(struct vsx_hash_table *hash_table);

#endif /* VSX_HASH_TABLE_H */
