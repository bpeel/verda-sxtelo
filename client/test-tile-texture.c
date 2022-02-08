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

#include "config.h"

#include "vsx-tile-texture.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#include "vsx-utf8.h"
#include "vsx-util.h"

static uint32_t
missing_letter_tests[] = {
        'A' - 1,
        'Z' + 1,
        0x0108 + 1, /* Ĉ */
        0x0108 - 1, /* Ĉ */
        0x011c + 1, /* Ĝ */
        0x011c - 1, /* Ĝ */
        0x0124 + 1, /* Ĥ */
        0x0124 - 1, /* Ĥ */
        0x0134 + 1, /* Ĵ */
        0x0134 - 1, /* Ĵ */
        0x015c + 1, /* Ŝ */
        0x015c - 1, /* Ŝ */
        0x016c + 1, /* Ŭ */
        0x016c - 1, /* Ŭ */
        'a',
        'z',
        ' ',
        0,
        UINT32_MAX,
};

static bool
test_missing_letter(uint32_t letter)
{
        const struct vsx_tile_texture_letter *letter_data =
                vsx_tile_texture_find_letter(letter);

        if (letter_data != NULL) {
                fprintf(stderr,
                        "Found letter data for U+%04x when none was "
                        "expected.\n",
                        letter);
                return false;
        }

        return true;
}

static bool
test_letter(uint32_t letter)
{
        char letter_name[8];

        letter_name[vsx_utf8_encode(letter, letter_name)] = '\0';

        const struct vsx_tile_texture_letter *letter_data =
                vsx_tile_texture_find_letter(letter);

        if (letter_data == NULL) {
                fprintf(stderr,
                        "Letter data for ‘%s’ not found.\n",
                        letter_name);
                return false;
        }

        if (letter_data->letter != letter) {
                fprintf(stderr,
                        "Expected letter U+%04x (%s) but got U+%04x\n",
                        letter,
                        letter_name,
                        letter_data->letter);
                return false;
        }

        return true;
}

int
main(int argc, char **argv)
{
        int ret = EXIT_SUCCESS;

        for (int i = 0; i < VSX_TILE_TEXTURE_N_LETTERS; i++) {
                if (!test_letter(vsx_tile_texture_letters[i].letter))
                        ret = EXIT_FAILURE;
        }

        for (int i = 0; i < VSX_N_ELEMENTS(missing_letter_tests); i++) {
                if (!test_missing_letter(missing_letter_tests[i]))
                        ret = EXIT_FAILURE;
        }

        return ret;
}
