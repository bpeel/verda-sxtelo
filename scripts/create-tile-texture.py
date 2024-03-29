#!/usr/bin/python3

# Verda Ŝtelo - An anagram game in Esperanto for the web
# Copyright (C) 2021  Neil Roberts
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.


import gi
gi.require_version('Pango', '1.0')
from gi.repository import Pango
gi.require_version('PangoCairo', '1.0')
from gi.repository import PangoCairo
import cairo
import math
import re
import os.path
import mako.template

LETTERS = list(sorted("ABCDEFGHIJKLMNOPQRSTUVWXYZĤŜĜĈĴŬ"))
N_LETTERS = len(LETTERS)

TILE_SIZE = 128
BORDER_SIZE = TILE_SIZE // 16

HEADER_TEMPLATE = mako.template.Template("""\
#ifndef VSX_TILE_TEXTURE_H
#define VSX_TILE_TEXTURE_H

#include <stdint.h>

#define VSX_TILE_TEXTURE_N_LETTERS ${n_letters}

struct vsx_tile_texture_letter {
        uint32_t letter;
        uint16_t s1, t1;
        uint16_t s2, t2;
};

extern const struct vsx_tile_texture_letter
vsx_tile_texture_letters[VSX_TILE_TEXTURE_N_LETTERS];

#endif /* VSX_TILE_TEXTURE_H */""")

SOURCE_TEMPLATE = mako.template.Template("""\
#include "vsx-tile-texture.h"

const struct vsx_tile_texture_letter
vsx_tile_texture_letters[VSX_TILE_TEXTURE_N_LETTERS] = {
% for letter_num, letter in enumerate(letters):
<%
  x1 = letter_num % x_tiles
  y1 = letter_num // x_tiles
  x2 = x1 + 1
  y2 = y1 + 1
%>\
        {
                .letter = ${ord(letter)}, /* ${letter} */
                .s1 = ${x1 * 65535 // x_tiles}, .t1 = ${y1 * 65535 // y_tiles},
                .s2 = ${x2 * 65535 // x_tiles}, .t2 = ${y2 * 65535 // y_tiles},
        },
% endfor
};""")


def output_copyright(outfile):
    had_line = False
    reg = re.compile(r'^#(?!!)')

    with open(__file__, "rt", encoding="utf-8") as f:
        print("/*", file=outfile)
        for line in f:
            if reg.match(line):
                had_line = True
                print(reg.sub(" *", line), end='', file=outfile)
            elif had_line:
                break

        print(" */\n", file=outfile)


def get_texture_size():
    w = 1
    h = 1

    while w * h < N_LETTERS:
        if w <= h:
            w *= 2
        else:
            h *= 2

    return w, h


def generate_header_file():
    header_file_name = os.path.join(os.path.dirname(__file__),
                                    "..",
                                    "client",
                                    "vsx-tile-texture.h")
    with open(header_file_name, "wt", encoding="utf-8") as f:
        output_copyright(f)

        print(HEADER_TEMPLATE.render(n_letters=N_LETTERS),
              file=f)


def generate_source_file():
    source_file_name = os.path.join(os.path.dirname(__file__),
                                    "..",
                                    "client",
                                    "vsx-tile-texture-letters.c")
    x_tiles, y_tiles = get_texture_size()

    with open(source_file_name, "wt", encoding="utf-8") as f:
        output_copyright(f)

        print(SOURCE_TEMPLATE.render(letters=LETTERS,
                                     x_tiles=x_tiles,
                                     y_tiles=y_tiles),
              file=f)


def generate_tile(cr, letter):
    pattern = cairo.LinearGradient(0.0, 0.0, 0.0, TILE_SIZE)
    pattern.add_color_stop_rgb(0.0, 1.0, 1.0, 1.0)
    pattern.add_color_stop_rgb(1.0, *([0xed / 255.0] * 3))
    cr.set_source(pattern)
    cr.rectangle(BORDER_SIZE,
                 BORDER_SIZE,
                 TILE_SIZE - BORDER_SIZE * 2,
                 TILE_SIZE - BORDER_SIZE * 2)
    cr.fill()

    cr.set_source_rgb(0.808, 0.835, 0.878)
    cr.move_to(0, TILE_SIZE)
    cr.line_to(BORDER_SIZE, TILE_SIZE - BORDER_SIZE)
    cr.line_to(BORDER_SIZE, BORDER_SIZE)
    cr.line_to(TILE_SIZE - BORDER_SIZE, BORDER_SIZE)
    cr.line_to(TILE_SIZE, 0)
    cr.line_to(0, 0)
    cr.close_path()
    cr.fill()

    cr.set_source_rgb(0.702, 0.745, 0.678)
    cr.move_to(0, TILE_SIZE)
    cr.line_to(BORDER_SIZE, TILE_SIZE - BORDER_SIZE)
    cr.line_to(TILE_SIZE - BORDER_SIZE, TILE_SIZE - BORDER_SIZE)
    cr.line_to(TILE_SIZE - BORDER_SIZE, BORDER_SIZE)
    cr.line_to(TILE_SIZE, 0)
    cr.line_to(TILE_SIZE, TILE_SIZE)
    cr.close_path()
    cr.fill()

    cr.set_source_rgb(0, 0, 0)

    layout = PangoCairo.create_layout(cr)
    layout.set_text(letter, -1)
    size = (TILE_SIZE - BORDER_SIZE * 2.0) * 0.8 * 72 / 96
    fd = Pango.FontDescription.from_string(f"Sans Serif {size}")
    layout.set_font_description(fd)
    (ink_rect, logical_rect) = layout.get_pixel_extents()

    cr.move_to(int(TILE_SIZE / 2.0 - logical_rect.x - logical_rect.width / 2.0),
               BORDER_SIZE + (TILE_SIZE - BORDER_SIZE * 2.0)
               - logical_rect.height
               + logical_rect.y)

    PangoCairo.show_layout(cr, layout)


def generate_tiles(cr, tiles_per_row):
    for tile_num, letter in enumerate(LETTERS):
        x = tile_num % tiles_per_row
        y = tile_num // tiles_per_row

        cr.save()

        cr.translate(x * TILE_SIZE, y * TILE_SIZE)

        generate_tile(cr, letter)

        cr.restore()


def generate_texture():
    x_tiles, y_tiles = get_texture_size()

    full_width = x_tiles * TILE_SIZE
    full_height = y_tiles * TILE_SIZE

    surface = cairo.ImageSurface(cairo.FORMAT_RGB24,
                                 full_width,
                                 full_height * 3 // 2)
    cr = cairo.Context(surface)

    scale_x = 1
    scale_y = 1
    x_pos = 0
    y_pos = 0
    level = 0

    while True:
        level_width = full_width // scale_x
        level_height = full_height // scale_y

        cr.save()
        cr.translate(x_pos, y_pos)
        cr.scale(1.0 / scale_x, 1.0 / scale_y)

        generate_tiles(cr, x_tiles)

        cr.restore()

        if level_width <= 1 and level_height <= 1:
            break;

        if (level & 1) == 0:
            y_pos += level_height
        else:
            x_pos += level_width

        level += 1

        if level_width > 1:
            scale_x *= 2
        if level_height > 1:
            scale_y *= 2

    return surface

generate_texture().write_to_png("tiles.mpng")

generate_header_file()
generate_source_file()
