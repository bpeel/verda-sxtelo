/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2024  Neil Roberts
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

/* Check that a font file has different glyphs for all of the Shavian
 * letters.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <freetype/freetype.h>

#define FIRST_SHAVIAN_CHARACTER 0x10450
#define N_SHAVIAN_CHARACTERS 48

static bool
test_font(FT_Library library,
          const char *filename)
{
        FT_Face face;

        if (FT_New_Face(library,
                        filename,
                        0, /* face_index */
                        &face) != 0) {
                fprintf(stderr, "%s: error opening font\n", filename);
                return false;
        }

        bool ret = true;
        unsigned glyphs[N_SHAVIAN_CHARACTERS] = { 0 };

        for (int i = 0; i < N_SHAVIAN_CHARACTERS; i++) {
                unsigned ch = FIRST_SHAVIAN_CHARACTER + i;
                unsigned glyph = FT_Get_Char_Index(face, ch);

                if (glyph == 0) {
                        fprintf(stderr,
                                "%s: missing glyph for 0x%x\n",
                                filename,
                                ch);
                        ret = false;
                        continue;
                }

                glyphs[i] = glyph;

                for (int j = 0; j < i; j++) {
                        if (glyphs[j] == glyph) {
                                fprintf(stderr,
                                        "%s: glyph for 0x%x is the same as "
                                        "for 0x%x\n",
                                        filename,
                                        ch,
                                        FIRST_SHAVIAN_CHARACTER + j);
                                ret = false;
                                break;
                        }
                }
        }

        return ret;
}

int
main(int argc, char **argv)
{
        int ret = EXIT_SUCCESS;

        if (argc < 2) {
                fprintf(stderr, "usage: test-font-shavian <font-file>...\n");
                return EXIT_FAILURE;
        }

        FT_Library ft_library;

        FT_Error ft_error = FT_Init_FreeType(&ft_library);

        if (ft_error != 0) {
                fprintf(stderr, "failed to initialise Freetype\n");
                return EXIT_FAILURE;
        }

        for (int i = 1; i < argc; i++) {
                if (!test_font(ft_library, argv[i]))
                        ret = EXIT_FAILURE;
        }

        FT_Done_FreeType(ft_library);

        return ret;
}
