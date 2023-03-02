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

#include "config.h"

#include "vsx-hash-table.h"

#include <assert.h>

#include "vsx-util.h"

static int
get_hash_pos(struct vsx_hash_table *hash_table,
             uint64_t id)
{
        return id % hash_table->table_size;
}

void
vsx_hash_table_destroy(struct vsx_hash_table *hash_table)
{
        vsx_free(hash_table->entries);
}

void
vsx_hash_table_remove(struct vsx_hash_table *hash_table,
                      struct vsx_hash_table_entry *entry)
{
        int pos = get_hash_pos(hash_table, entry->id);
        struct vsx_hash_table_entry **prev = hash_table->entries + pos;

        while (true) {
                assert(*prev);

                if (*prev == entry)
                        break;

                prev = &(*prev)->next;
        }

        *prev = entry->next;

        hash_table->n_entries--;
}

void
vsx_hash_table_init(struct vsx_hash_table *hash_table)
{
        hash_table->n_entries = 0;
        hash_table->table_size = 8;
        hash_table->entries = vsx_calloc(hash_table->table_size *
                                         sizeof *hash_table->entries);
}

static void
add_entry_to_hash(struct vsx_hash_table *hash_table,
                  struct vsx_hash_table_entry *entry)
{
        int pos = get_hash_pos(hash_table, entry->id);

        entry->next = hash_table->entries[pos];
        hash_table->entries[pos] = entry;
}

static void
grow_hash_table(struct vsx_hash_table *hash_table)
{
        struct vsx_hash_table_entry *entry_list;
        struct vsx_hash_table_entry **prev = &entry_list;

        /* Gather all the entries into a list */
        for (unsigned i = 0; i < hash_table->table_size; i++) {
                for (struct vsx_hash_table_entry *e = hash_table->entries[i];
                     e;
                     e = e->next) {
                        *prev = e;
                        prev = &e->next;
                }
        }

        *prev = NULL;

        vsx_free(hash_table->entries);

        hash_table->table_size *= 2;

        hash_table->entries = vsx_calloc(hash_table->table_size *
                                         sizeof *hash_table->entries);

        struct vsx_hash_table_entry *next;

        for (struct vsx_hash_table_entry *entry = entry_list;
             entry;
             entry = next) {
                next = entry->next;

                add_entry_to_hash(hash_table, entry);
        }
}

struct vsx_hash_table_entry *
vsx_hash_table_get(struct vsx_hash_table *hash_table,
                   uint64_t key)
{
        int pos = get_hash_pos(hash_table, key);

        for (struct vsx_hash_table_entry *entry = hash_table->entries[pos];
             entry;
             entry = entry->next) {
                if (entry->id == key)
                        return entry;
        }

        return NULL;
}

void
vsx_hash_table_add(struct vsx_hash_table *hash_table,
                   struct vsx_hash_table_entry *entry)
{
        if ((hash_table->n_entries + 1) > hash_table->table_size * 3 / 4)
                grow_hash_table(hash_table);

        add_entry_to_hash(hash_table, entry);

        hash_table->n_entries++;
}
