/*
 * Enketo - A geolocalisation game.
 * Copyright (C) 2021 Neil Roberts
 */

#include "config.h"

#include <assert.h>
#include <stdlib.h>

#include "vsx-glyph-hash.h"

int
main(int argc, char **argv)
{
        struct vsx_glyph_hash *hash = vsx_glyph_hash_new();

        /* Add a bunch of entries */
        for (int i = 0; i < 3000; i++) {
                bool added = false;
                struct vsx_glyph_hash_entry *entry =
                        vsx_glyph_hash_get(hash, i, &added);

                assert(entry->hash_entry.id == i);
                assert(added);

                /* Fill in some test values so that we can recognise
                 * it when it comes back.
                 */
                entry->x_advance = i;
                entry->tex_num = i + 1;
                entry->s1 = i + 2;
                entry->t1 = i + 3;
                entry->s2 = i + 4;
                entry->t2 = i + 5;

                /* Check that all of the entries added so far still
                 * get the same result.
                 */
                for (int j = 0; j < i; j++) {
                        added = true;
                        entry = vsx_glyph_hash_get(hash, j, &added);

                        assert(!added);
                        assert(entry->hash_entry.id == j);
                        assert(entry->x_advance == j);
                        assert(entry->tex_num == j + 1);
                        assert(entry->s1 == j + 2);
                        assert(entry->t1 == j + 3);
                        assert(entry->s2 == j + 4);
                        assert(entry->t2 == j + 5);
                }
        }

        vsx_glyph_hash_free(hash);

        return EXIT_SUCCESS;
}
