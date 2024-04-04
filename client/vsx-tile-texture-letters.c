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
                .texture = 0,
                .s1 = 0, .t1 = 0,
                .s2 = 8192, .t2 = 16384,
        },
        {
                .letter = 66, /* B */
                .texture = 0,
                .s1 = 8192, .t1 = 0,
                .s2 = 16384, .t2 = 16384,
        },
        {
                .letter = 67, /* C */
                .texture = 0,
                .s1 = 16384, .t1 = 0,
                .s2 = 24576, .t2 = 16384,
        },
        {
                .letter = 68, /* D */
                .texture = 0,
                .s1 = 24576, .t1 = 0,
                .s2 = 32768, .t2 = 16384,
        },
        {
                .letter = 69, /* E */
                .texture = 0,
                .s1 = 32768, .t1 = 0,
                .s2 = 40959, .t2 = 16384,
        },
        {
                .letter = 70, /* F */
                .texture = 0,
                .s1 = 40959, .t1 = 0,
                .s2 = 49151, .t2 = 16384,
        },
        {
                .letter = 71, /* G */
                .texture = 0,
                .s1 = 49151, .t1 = 0,
                .s2 = 57343, .t2 = 16384,
        },
        {
                .letter = 72, /* H */
                .texture = 0,
                .s1 = 57343, .t1 = 0,
                .s2 = 65535, .t2 = 16384,
        },
        {
                .letter = 73, /* I */
                .texture = 0,
                .s1 = 0, .t1 = 16384,
                .s2 = 8192, .t2 = 32768,
        },
        {
                .letter = 74, /* J */
                .texture = 0,
                .s1 = 8192, .t1 = 16384,
                .s2 = 16384, .t2 = 32768,
        },
        {
                .letter = 75, /* K */
                .texture = 0,
                .s1 = 16384, .t1 = 16384,
                .s2 = 24576, .t2 = 32768,
        },
        {
                .letter = 76, /* L */
                .texture = 0,
                .s1 = 24576, .t1 = 16384,
                .s2 = 32768, .t2 = 32768,
        },
        {
                .letter = 77, /* M */
                .texture = 0,
                .s1 = 32768, .t1 = 16384,
                .s2 = 40959, .t2 = 32768,
        },
        {
                .letter = 78, /* N */
                .texture = 0,
                .s1 = 40959, .t1 = 16384,
                .s2 = 49151, .t2 = 32768,
        },
        {
                .letter = 79, /* O */
                .texture = 0,
                .s1 = 49151, .t1 = 16384,
                .s2 = 57343, .t2 = 32768,
        },
        {
                .letter = 80, /* P */
                .texture = 0,
                .s1 = 57343, .t1 = 16384,
                .s2 = 65535, .t2 = 32768,
        },
        {
                .letter = 81, /* Q */
                .texture = 0,
                .s1 = 0, .t1 = 32768,
                .s2 = 8192, .t2 = 49151,
        },
        {
                .letter = 82, /* R */
                .texture = 0,
                .s1 = 8192, .t1 = 32768,
                .s2 = 16384, .t2 = 49151,
        },
        {
                .letter = 83, /* S */
                .texture = 0,
                .s1 = 16384, .t1 = 32768,
                .s2 = 24576, .t2 = 49151,
        },
        {
                .letter = 84, /* T */
                .texture = 0,
                .s1 = 24576, .t1 = 32768,
                .s2 = 32768, .t2 = 49151,
        },
        {
                .letter = 85, /* U */
                .texture = 0,
                .s1 = 32768, .t1 = 32768,
                .s2 = 40959, .t2 = 49151,
        },
        {
                .letter = 86, /* V */
                .texture = 0,
                .s1 = 40959, .t1 = 32768,
                .s2 = 49151, .t2 = 49151,
        },
        {
                .letter = 87, /* W */
                .texture = 0,
                .s1 = 49151, .t1 = 32768,
                .s2 = 57343, .t2 = 49151,
        },
        {
                .letter = 88, /* X */
                .texture = 0,
                .s1 = 57343, .t1 = 32768,
                .s2 = 65535, .t2 = 49151,
        },
        {
                .letter = 89, /* Y */
                .texture = 0,
                .s1 = 0, .t1 = 49151,
                .s2 = 8192, .t2 = 65535,
        },
        {
                .letter = 90, /* Z */
                .texture = 0,
                .s1 = 8192, .t1 = 49151,
                .s2 = 16384, .t2 = 65535,
        },
        {
                .letter = 264, /* Ĉ */
                .texture = 0,
                .s1 = 16384, .t1 = 49151,
                .s2 = 24576, .t2 = 65535,
        },
        {
                .letter = 284, /* Ĝ */
                .texture = 0,
                .s1 = 24576, .t1 = 49151,
                .s2 = 32768, .t2 = 65535,
        },
        {
                .letter = 292, /* Ĥ */
                .texture = 0,
                .s1 = 32768, .t1 = 49151,
                .s2 = 40959, .t2 = 65535,
        },
        {
                .letter = 308, /* Ĵ */
                .texture = 0,
                .s1 = 40959, .t1 = 49151,
                .s2 = 49151, .t2 = 65535,
        },
        {
                .letter = 348, /* Ŝ */
                .texture = 0,
                .s1 = 49151, .t1 = 49151,
                .s2 = 57343, .t2 = 65535,
        },
        {
                .letter = 364, /* Ŭ */
                .texture = 0,
                .s1 = 57343, .t1 = 49151,
                .s2 = 65535, .t2 = 65535,
        },
        {
                .letter = 66640, /* 𐑐 */
                .texture = 1,
                .s1 = 0, .t1 = 0,
                .s2 = 8192, .t2 = 8192,
        },
        {
                .letter = 66641, /* 𐑑 */
                .texture = 1,
                .s1 = 8192, .t1 = 0,
                .s2 = 16384, .t2 = 8192,
        },
        {
                .letter = 66642, /* 𐑒 */
                .texture = 1,
                .s1 = 16384, .t1 = 0,
                .s2 = 24576, .t2 = 8192,
        },
        {
                .letter = 66643, /* 𐑓 */
                .texture = 1,
                .s1 = 24576, .t1 = 0,
                .s2 = 32768, .t2 = 8192,
        },
        {
                .letter = 66644, /* 𐑔 */
                .texture = 1,
                .s1 = 32768, .t1 = 0,
                .s2 = 40959, .t2 = 8192,
        },
        {
                .letter = 66645, /* 𐑕 */
                .texture = 1,
                .s1 = 40959, .t1 = 0,
                .s2 = 49151, .t2 = 8192,
        },
        {
                .letter = 66646, /* 𐑖 */
                .texture = 1,
                .s1 = 49151, .t1 = 0,
                .s2 = 57343, .t2 = 8192,
        },
        {
                .letter = 66647, /* 𐑗 */
                .texture = 1,
                .s1 = 57343, .t1 = 0,
                .s2 = 65535, .t2 = 8192,
        },
        {
                .letter = 66648, /* 𐑘 */
                .texture = 1,
                .s1 = 0, .t1 = 8192,
                .s2 = 8192, .t2 = 16384,
        },
        {
                .letter = 66649, /* 𐑙 */
                .texture = 1,
                .s1 = 8192, .t1 = 8192,
                .s2 = 16384, .t2 = 16384,
        },
        {
                .letter = 66650, /* 𐑚 */
                .texture = 1,
                .s1 = 16384, .t1 = 8192,
                .s2 = 24576, .t2 = 16384,
        },
        {
                .letter = 66651, /* 𐑛 */
                .texture = 1,
                .s1 = 24576, .t1 = 8192,
                .s2 = 32768, .t2 = 16384,
        },
        {
                .letter = 66652, /* 𐑜 */
                .texture = 1,
                .s1 = 32768, .t1 = 8192,
                .s2 = 40959, .t2 = 16384,
        },
        {
                .letter = 66653, /* 𐑝 */
                .texture = 1,
                .s1 = 40959, .t1 = 8192,
                .s2 = 49151, .t2 = 16384,
        },
        {
                .letter = 66654, /* 𐑞 */
                .texture = 1,
                .s1 = 49151, .t1 = 8192,
                .s2 = 57343, .t2 = 16384,
        },
        {
                .letter = 66655, /* 𐑟 */
                .texture = 1,
                .s1 = 57343, .t1 = 8192,
                .s2 = 65535, .t2 = 16384,
        },
        {
                .letter = 66656, /* 𐑠 */
                .texture = 1,
                .s1 = 0, .t1 = 16384,
                .s2 = 8192, .t2 = 24576,
        },
        {
                .letter = 66657, /* 𐑡 */
                .texture = 1,
                .s1 = 8192, .t1 = 16384,
                .s2 = 16384, .t2 = 24576,
        },
        {
                .letter = 66658, /* 𐑢 */
                .texture = 1,
                .s1 = 16384, .t1 = 16384,
                .s2 = 24576, .t2 = 24576,
        },
        {
                .letter = 66659, /* 𐑣 */
                .texture = 1,
                .s1 = 24576, .t1 = 16384,
                .s2 = 32768, .t2 = 24576,
        },
        {
                .letter = 66660, /* 𐑤 */
                .texture = 1,
                .s1 = 32768, .t1 = 16384,
                .s2 = 40959, .t2 = 24576,
        },
        {
                .letter = 66661, /* 𐑥 */
                .texture = 1,
                .s1 = 40959, .t1 = 16384,
                .s2 = 49151, .t2 = 24576,
        },
        {
                .letter = 66662, /* 𐑦 */
                .texture = 1,
                .s1 = 49151, .t1 = 16384,
                .s2 = 57343, .t2 = 24576,
        },
        {
                .letter = 66663, /* 𐑧 */
                .texture = 1,
                .s1 = 57343, .t1 = 16384,
                .s2 = 65535, .t2 = 24576,
        },
        {
                .letter = 66664, /* 𐑨 */
                .texture = 1,
                .s1 = 0, .t1 = 24576,
                .s2 = 8192, .t2 = 32768,
        },
        {
                .letter = 66665, /* 𐑩 */
                .texture = 1,
                .s1 = 8192, .t1 = 24576,
                .s2 = 16384, .t2 = 32768,
        },
        {
                .letter = 66666, /* 𐑪 */
                .texture = 1,
                .s1 = 16384, .t1 = 24576,
                .s2 = 24576, .t2 = 32768,
        },
        {
                .letter = 66667, /* 𐑫 */
                .texture = 1,
                .s1 = 24576, .t1 = 24576,
                .s2 = 32768, .t2 = 32768,
        },
        {
                .letter = 66668, /* 𐑬 */
                .texture = 1,
                .s1 = 32768, .t1 = 24576,
                .s2 = 40959, .t2 = 32768,
        },
        {
                .letter = 66669, /* 𐑭 */
                .texture = 1,
                .s1 = 40959, .t1 = 24576,
                .s2 = 49151, .t2 = 32768,
        },
        {
                .letter = 66670, /* 𐑮 */
                .texture = 1,
                .s1 = 49151, .t1 = 24576,
                .s2 = 57343, .t2 = 32768,
        },
        {
                .letter = 66671, /* 𐑯 */
                .texture = 1,
                .s1 = 57343, .t1 = 24576,
                .s2 = 65535, .t2 = 32768,
        },
        {
                .letter = 66672, /* 𐑰 */
                .texture = 1,
                .s1 = 0, .t1 = 32768,
                .s2 = 8192, .t2 = 40959,
        },
        {
                .letter = 66673, /* 𐑱 */
                .texture = 1,
                .s1 = 8192, .t1 = 32768,
                .s2 = 16384, .t2 = 40959,
        },
        {
                .letter = 66674, /* 𐑲 */
                .texture = 1,
                .s1 = 16384, .t1 = 32768,
                .s2 = 24576, .t2 = 40959,
        },
        {
                .letter = 66675, /* 𐑳 */
                .texture = 1,
                .s1 = 24576, .t1 = 32768,
                .s2 = 32768, .t2 = 40959,
        },
        {
                .letter = 66676, /* 𐑴 */
                .texture = 1,
                .s1 = 32768, .t1 = 32768,
                .s2 = 40959, .t2 = 40959,
        },
        {
                .letter = 66677, /* 𐑵 */
                .texture = 1,
                .s1 = 40959, .t1 = 32768,
                .s2 = 49151, .t2 = 40959,
        },
        {
                .letter = 66678, /* 𐑶 */
                .texture = 1,
                .s1 = 49151, .t1 = 32768,
                .s2 = 57343, .t2 = 40959,
        },
        {
                .letter = 66679, /* 𐑷 */
                .texture = 1,
                .s1 = 57343, .t1 = 32768,
                .s2 = 65535, .t2 = 40959,
        },
        {
                .letter = 66680, /* 𐑸 */
                .texture = 1,
                .s1 = 0, .t1 = 40959,
                .s2 = 8192, .t2 = 49151,
        },
        {
                .letter = 66681, /* 𐑹 */
                .texture = 1,
                .s1 = 8192, .t1 = 40959,
                .s2 = 16384, .t2 = 49151,
        },
        {
                .letter = 66682, /* 𐑺 */
                .texture = 1,
                .s1 = 16384, .t1 = 40959,
                .s2 = 24576, .t2 = 49151,
        },
        {
                .letter = 66683, /* 𐑻 */
                .texture = 1,
                .s1 = 24576, .t1 = 40959,
                .s2 = 32768, .t2 = 49151,
        },
        {
                .letter = 66684, /* 𐑼 */
                .texture = 1,
                .s1 = 32768, .t1 = 40959,
                .s2 = 40959, .t2 = 49151,
        },
        {
                .letter = 66685, /* 𐑽 */
                .texture = 1,
                .s1 = 40959, .t1 = 40959,
                .s2 = 49151, .t2 = 49151,
        },
        {
                .letter = 66686, /* 𐑾 */
                .texture = 1,
                .s1 = 49151, .t1 = 40959,
                .s2 = 57343, .t2 = 49151,
        },
        {
                .letter = 66687, /* 𐑿 */
                .texture = 1,
                .s1 = 57343, .t1 = 40959,
                .s2 = 65535, .t2 = 49151,
        },
};
