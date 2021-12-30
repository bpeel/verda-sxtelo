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

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <inttypes.h>

#include "vsx-hash-table.h"
#include "vsx-list.h"
#include "vsx-util.h"

struct harness {
        struct vsx_list entries;
        struct vsx_hash_table hash_table;
};

struct test_entry {
        struct vsx_list link;
        struct vsx_hash_table_entry entry;
};

static bool
check_all_entries(struct harness *harness)
{
        int n_entries = vsx_list_length(&harness->entries);

        if (n_entries != harness->hash_table.n_entries) {
                fprintf(stderr,
                        "%i entries are in the test list but %i are in the "
                        "hash table\n",
                        n_entries,
                        harness->hash_table.n_entries);
                return false;
        }

        struct test_entry *entry;

        vsx_list_for_each(entry, &harness->entries, link) {
                struct vsx_hash_table_entry *table_entry =
                        vsx_hash_table_get(&harness->hash_table,
                                           entry->entry.id);

                if (table_entry == NULL) {
                        fprintf(stderr,
                                "Missing entry 0x%" PRIx64 " in hash table\n",
                                entry->entry.id);
                        return false;
                }

                struct test_entry *found_entry =
                        vsx_container_of(table_entry,
                                         struct test_entry,
                                         entry);

                if (entry != found_entry) {
                        fprintf(stderr,
                                "Looked for entry 0x%" PRIx64 " but got "
                                "0x%" PRIx64 "\n",
                                entry->entry.id,
                                found_entry->entry.id);
                        return false;
                }
        }

        return true;
}

static struct test_entry *
add_entry(struct harness *harness,
          uint64_t id)
{
        struct test_entry *test_entry = vsx_alloc(sizeof *test_entry);

        test_entry->entry.id = id;
        vsx_list_insert(harness->entries.prev, &test_entry->link);

        vsx_hash_table_add(&harness->hash_table, &test_entry->entry);

        return test_entry;
}

static void
remove_entry(struct harness *harness,
             struct test_entry *test_entry)
{
        vsx_hash_table_remove(&harness->hash_table, &test_entry->entry);
        vsx_list_remove(&test_entry->link);
        vsx_free(test_entry);
}

static bool
test_collision(struct harness *harness, bool reverse_remove)
{
        if (!check_all_entries(harness))
                return false;

        struct test_entry *eight = add_entry(harness, 8);

        if (!check_all_entries(harness))
                return false;

        /* Add an entry that should share the same position */
        struct test_entry *sixteen = add_entry(harness, 16);

        if (!check_all_entries(harness))
                return false;

        if (sixteen->entry.next != &eight->entry) {
                fprintf(stderr,
                        "The test failed to make a hash collision.\n");
                return false;
        }

        struct test_entry *a, *b;

        if (reverse_remove) {
                a = sixteen;
                b = eight;
        } else {
                a = eight;
                b = sixteen;
        }

        remove_entry(harness, a);

        if (!check_all_entries(harness))
                return false;

        remove_entry(harness, b);

        if (!check_all_entries(harness))
                return false;

        return true;
}

static struct test_entry *
find_entry(struct harness *harness, uint64_t id)
{
        struct test_entry *entry;

        vsx_list_for_each(entry, &harness->entries, link) {
                if (entry->entry.id == id)
                        return entry;
        }

        return NULL;
}

static bool
test_add_many(struct harness *harness)
{
        const int n_entries = 7 * 3 * 200;

        for (int i = 0; i < n_entries; i++) {
                add_entry(harness, i);
                if (!check_all_entries(harness))
                        return false;
        }

        for (int i = 0; i < n_entries; i++) {
                /* Remove them in a strange order */
                uint64_t id = i / 7 * 7 + (6 - i % 7);

                struct test_entry *entry = find_entry(harness, id);

                remove_entry(harness, entry);

                if (!check_all_entries(harness))
                        return false;

                if (vsx_hash_table_get(&harness->hash_table, id)) {
                        fprintf(stderr,
                                "ID still in hash table after being removed\n");
                        return false;
                }
        }

        return true;
}

static bool
run_tests(struct harness *harness)
{
        if (!test_collision(harness, false /* reverse_remove */))
                return false;

        if (!test_collision(harness, true /* reverse_remove */))
                return false;

        if (!test_add_many(harness))
                return false;

        return true;
}

int
main(int argc, char **argv)
{
        struct harness harness;
        int ret = EXIT_SUCCESS;

        vsx_list_init(&harness.entries);
        vsx_hash_table_init(&harness.hash_table);

        if (!run_tests(&harness))
                ret = EXIT_FAILURE;

        vsx_hash_table_destroy(&harness.hash_table);

        return ret;
}
