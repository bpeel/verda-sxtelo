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

#include "vsx-tile-texture.h"

const struct vsx_tile_texture_letter
vsx_tile_texture_letters[VSX_TILE_TEXTURE_N_LETTERS] = {
        {
                .letter = 65, /* A */
                .s1 = 0, .t1 = 0,
                .s2 = 8191, .t2 = 16383,
        },
        {
                .letter = 66, /* B */
                .s1 = 8191, .t1 = 0,
                .s2 = 16383, .t2 = 16383,
        },
        {
                .letter = 67, /* C */
                .s1 = 16383, .t1 = 0,
                .s2 = 24575, .t2 = 16383,
        },
        {
                .letter = 68, /* D */
                .s1 = 24575, .t1 = 0,
                .s2 = 32767, .t2 = 16383,
        },
        {
                .letter = 69, /* E */
                .s1 = 32767, .t1 = 0,
                .s2 = 40959, .t2 = 16383,
        },
        {
                .letter = 70, /* F */
                .s1 = 40959, .t1 = 0,
                .s2 = 49151, .t2 = 16383,
        },
        {
                .letter = 71, /* G */
                .s1 = 49151, .t1 = 0,
                .s2 = 57343, .t2 = 16383,
        },
        {
                .letter = 72, /* H */
                .s1 = 57343, .t1 = 0,
                .s2 = 65535, .t2 = 16383,
        },
        {
                .letter = 73, /* I */
                .s1 = 0, .t1 = 16383,
                .s2 = 8191, .t2 = 32767,
        },
        {
                .letter = 74, /* J */
                .s1 = 8191, .t1 = 16383,
                .s2 = 16383, .t2 = 32767,
        },
        {
                .letter = 75, /* K */
                .s1 = 16383, .t1 = 16383,
                .s2 = 24575, .t2 = 32767,
        },
        {
                .letter = 76, /* L */
                .s1 = 24575, .t1 = 16383,
                .s2 = 32767, .t2 = 32767,
        },
        {
                .letter = 77, /* M */
                .s1 = 32767, .t1 = 16383,
                .s2 = 40959, .t2 = 32767,
        },
        {
                .letter = 78, /* N */
                .s1 = 40959, .t1 = 16383,
                .s2 = 49151, .t2 = 32767,
        },
        {
                .letter = 79, /* O */
                .s1 = 49151, .t1 = 16383,
                .s2 = 57343, .t2 = 32767,
        },
        {
                .letter = 80, /* P */
                .s1 = 57343, .t1 = 16383,
                .s2 = 65535, .t2 = 32767,
        },
        {
                .letter = 81, /* Q */
                .s1 = 0, .t1 = 32767,
                .s2 = 8191, .t2 = 49151,
        },
        {
                .letter = 82, /* R */
                .s1 = 8191, .t1 = 32767,
                .s2 = 16383, .t2 = 49151,
        },
        {
                .letter = 83, /* S */
                .s1 = 16383, .t1 = 32767,
                .s2 = 24575, .t2 = 49151,
        },
        {
                .letter = 84, /* T */
                .s1 = 24575, .t1 = 32767,
                .s2 = 32767, .t2 = 49151,
        },
        {
                .letter = 85, /* U */
                .s1 = 32767, .t1 = 32767,
                .s2 = 40959, .t2 = 49151,
        },
        {
                .letter = 86, /* V */
                .s1 = 40959, .t1 = 32767,
                .s2 = 49151, .t2 = 49151,
        },
        {
                .letter = 87, /* W */
                .s1 = 49151, .t1 = 32767,
                .s2 = 57343, .t2 = 49151,
        },
        {
                .letter = 88, /* X */
                .s1 = 57343, .t1 = 32767,
                .s2 = 65535, .t2 = 49151,
        },
        {
                .letter = 89, /* Y */
                .s1 = 0, .t1 = 49151,
                .s2 = 8191, .t2 = 65535,
        },
        {
                .letter = 90, /* Z */
                .s1 = 8191, .t1 = 49151,
                .s2 = 16383, .t2 = 65535,
        },
        {
                .letter = 264, /* Ĉ */
                .s1 = 16383, .t1 = 49151,
                .s2 = 24575, .t2 = 65535,
        },
        {
                .letter = 284, /* Ĝ */
                .s1 = 24575, .t1 = 49151,
                .s2 = 32767, .t2 = 65535,
        },
        {
                .letter = 292, /* Ĥ */
                .s1 = 32767, .t1 = 49151,
                .s2 = 40959, .t2 = 65535,
        },
        {
                .letter = 308, /* Ĵ */
                .s1 = 40959, .t1 = 49151,
                .s2 = 49151, .t2 = 65535,
        },
        {
                .letter = 348, /* Ŝ */
                .s1 = 49151, .t1 = 49151,
                .s2 = 57343, .t2 = 65535,
        },
        {
                .letter = 364, /* Ŭ */
                .s1 = 57343, .t1 = 49151,
                .s2 = 65535, .t2 = 65535,
        },
};
