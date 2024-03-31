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

/* Check that a font file has all the glyphs needed for French,
 * Esperanto and English in Latin and Shavian.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <freetype/freetype.h>

#include "vsx-util.h"

static const uint32_t chars[] = {
        'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
        'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
        'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',

        0x0125, /* ĥ */
        0x015d, /* ŝ */
        0x011d, /* ĝ */
        0x0109, /* ĉ */
        0x0135, /* ĵ */
        0x016d, /* ŭ */
        0x0124, /* Ĥ */
        0x015c, /* Ŝ */
        0x011c, /* Ĝ */
        0x0108, /* Ĉ */
        0x0134, /* Ĵ */
        0x016c, /* Ŭ */

        0x00e0, /* à */
        0x00e2, /* â */
        0x00e9, /* é */
        0x00e8, /* è */
        0x00ea, /* ê */
        0x00eb, /* ë */
        0x00ee, /* î */
        0x00ef, /* ï */
        0x00f4, /* ô */
        0x00f9, /* ù */
        0x00fb, /* û */
        0x00fc, /* ü */
        0x00ff, /* ÿ */
        0x00e7, /* ç */
        0x00e6, /* æ */
        0x0153, /* œ */

        0x00c0, /* À */
        0x00c2, /* Â */
        0x00c9, /* É */
        0x00c8, /* È */
        0x00ca, /* Ê */
        0x00cb, /* Ë */
        0x00ce, /* Î */
        0x00cf, /* Ï */
        0x00d4, /* Ô */
        0x00d9, /* Ù */
        0x00db, /* Û */
        0x00dc, /* Ü */
        0x0178, /* Ÿ */
        0x00c7, /* Ç */
        0x00c6, /* Æ */
        0x0152, /* Œ */

        0x10450, /* 𐑐 */
        0x10451, /* 𐑑 */
        0x10452, /* 𐑒 */
        0x10453, /* 𐑓 */
        0x10454, /* 𐑔 */
        0x10455, /* 𐑕 */
        0x10456, /* 𐑖 */
        0x10457, /* 𐑗 */
        0x10458, /* 𐑘 */
        0x10459, /* 𐑙 */
        0x1045a, /* 𐑚 */
        0x1045b, /* 𐑛 */
        0x1045c, /* 𐑜 */
        0x1045d, /* 𐑝 */
        0x1045e, /* 𐑞 */
        0x1045f, /* 𐑟 */
        0x10460, /* 𐑠 */
        0x10461, /* 𐑡 */
        0x10462, /* 𐑢 */
        0x10463, /* 𐑣 */
        0x10464, /* 𐑤 */
        0x10465, /* 𐑥 */
        0x10466, /* 𐑦 */
        0x10467, /* 𐑧 */
        0x10468, /* 𐑨 */
        0x10469, /* 𐑩 */
        0x1046a, /* 𐑪 */
        0x1046b, /* 𐑫 */
        0x1046c, /* 𐑬 */
        0x1046d, /* 𐑭 */
        0x1046e, /* 𐑮 */
        0x1046f, /* 𐑯 */
        0x10470, /* 𐑰 */
        0x10471, /* 𐑱 */
        0x10472, /* 𐑲 */
        0x10473, /* 𐑳 */
        0x10474, /* 𐑴 */
        0x10475, /* 𐑵 */
        0x10476, /* 𐑶 */
        0x10477, /* 𐑷 */
        0x10478, /* 𐑸 */
        0x10479, /* 𐑹 */
        0x1047a, /* 𐑺 */
        0x1047b, /* 𐑻 */
        0x1047c, /* 𐑼 */
        0x1047d, /* 𐑽 */
        0x1047e, /* 𐑾 */
        0x1047f, /* 𐑿 */
};

#define N_CHARACTERS VSX_N_ELEMENTS(chars)

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
        unsigned glyphs[N_CHARACTERS] = { 0 };

        for (int i = 0; i < N_CHARACTERS; i++) {
                unsigned ch = chars[i];
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
                                        chars[j]);
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
