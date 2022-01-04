#!/usr/bin/python3

# Verda Ŝtelo - An anagram game in Esperanto for the web
# Copyright (C) 2022  Neil Roberts
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

# This is used to create the base images for a version 3 QR code. The
# base image contains the finder, alignment and timing patterns. The
# image is stored as one integer per row, where the first integer
# represents the topmost row. Within the integer bit 0 represents the
# leftmost part of the image.


import PIL.Image
import os
import re


N_MODULES = 29
QUIET_ZONE_SIZE = 4

ALIGNMENT_PATTERNS = [(22 - 2, 22 - 2)]

MASK_FUNCTIONS = [
    lambda x, y: ((x + y) & 1) == 0,
    lambda x, y: (y & 1) == 0,
    lambda x, y: x % 3 == 0,
    lambda x, y: (x + y) % 3 == 0,
    lambda x, y: (((y // 2) + (x // 3)) & 1) == 0,
    lambda x, y: ((x * y) & 1) + (x * y) % 3 == 0,
    lambda x, y: ((((x * y) & 1) + (x * y) % 3) & 1) == 0,
    lambda x, y: ((((x + y) & 1) + (x * y) % 3) & 1) == 0,
]


class QrImage:
    def __init__(self):
        self.bits = [ 0 ] * N_MODULES

    def set_module(self, x, y):
        self.bits[y] |= 1 << x

    def set_rectangle(self, x, y, w, h):
        for i in range(w):
            for j in range(h):
                self.set_module(x + i, y + j)

    def check_module(self, x, y):
        return (self.bits[y] & (1 << x)) != 0

    def invert(self):
        for i in range(N_MODULES):
            self.bits[i] ^= (1 << N_MODULES) - 1

    def mask(self, other):
        for i in range(N_MODULES):
            self.bits[i] &= other.bits[i]

    def nums_to_c(self, indent, outfile):
        print(" " * indent + ".bits = {", file=outfile)
        for num in self.bits:
            print(" " * (indent + 8) + "UINT32_C(0x{:08x}),".format(num),
                  file=outfile)
        print(" " * indent + "},", file=outfile)

    def to_c(self, name, outfile):
        print(("static const struct vsx_qr_image\n"
               "{} = {{").format(name),
              file=outfile)
        self.nums_to_c(8, outfile)
        print("};\n",
              file=outfile)

    def to_pil(self):
        image = PIL.Image.new("1", # mode
                              (N_MODULES + QUIET_ZONE_SIZE * 2,
                               N_MODULES + QUIET_ZONE_SIZE * 2),
                              color=1)

        for y in range(N_MODULES):
            for x in range(N_MODULES):
                if self.check_module(x, y):
                    image.putpixel((x + QUIET_ZONE_SIZE,
                                    y + QUIET_ZONE_SIZE),
                                   0)

        return image


def draw_finder_pattern(image, x, y):
    for i in range(7):
        image.set_module(x + i, y)
        image.set_module(x + i, y + 6)

    for i in range(5):
        image.set_module(x, y + i + 1)
        image.set_module(x + 6, y + i + 1)

    x += 2
    y += 2

    for i in range(3):
        for j in range(3):
            image.set_module(x + i, y + j)


def draw_finder_patterns(image):
    draw_finder_pattern(image, 0, 0)
    draw_finder_pattern(image, N_MODULES - 7, 0)
    draw_finder_pattern(image, 0, N_MODULES - 7)


def draw_timing_patterns(image):
    for i in range(8, N_MODULES - 8, 2):
        image.set_module(i, 6)
        image.set_module(6, i)


def draw_alignment_pattern(image, x, y):
    for i in range(5):
        image.set_module(x + i, y)
        image.set_module(x + i, y + 4)

    for i in range(3):
        image.set_module(x, y + 1 + i)
        image.set_module(x + 4, y + 1 + i)

    image.set_module(x + 2, y + 2)


def draw_alignment_patterns(image):
    for x, y in ALIGNMENT_PATTERNS:
        draw_alignment_pattern(image, x, y)


def create_base_image():
    image = QrImage()
    draw_finder_patterns(image)
    draw_timing_patterns(image)
    draw_alignment_patterns(image)
    # Dark module is always set
    image.set_module(8, N_MODULES - 8)
    return image


def create_mask_image(data_mask_image, mask_func):
    image = QrImage()

    for y in range(N_MODULES):
        for x in range(N_MODULES):
            if mask_func(x, y):
                image.set_module(x, y)

    image.mask(data_mask_image)

    return image


# Creates an image to mask out the areas that can’t contain data.
# These are also the parts that should be masked with the XOR-mask.
def create_data_mask_image():
    image = QrImage()
    # The finder patterns + 1-pixel gap around
    image.set_rectangle(0, 0, 8, 8)
    image.set_rectangle(N_MODULES - 8, 0, 8, 8)
    image.set_rectangle(0, N_MODULES - 8, 8, 8)

    # The timing patterns
    image.set_rectangle(8, 6, N_MODULES - 16, 1)
    image.set_rectangle(6, 8, 1, N_MODULES - 16)

    # Alignment patterns
    for x, y in ALIGNMENT_PATTERNS:
        image.set_rectangle(x, y, 5, 5)

    # Format bits
    image.set_rectangle(0, 8, 9, 1)
    image.set_rectangle(8, 0, 1, 8)
    image.set_rectangle(8, N_MODULES - 8, 1, 8)
    image.set_rectangle(N_MODULES - 8, 8, 8, 1)

    # Invert the image so the bit is set for the areas that we can draw to
    image.invert()

    return image


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


def generate_header_file():
    header_file_name = os.path.join(os.path.dirname(__file__),
                                    "..",
                                    "client",
                                    "vsx-qr-data.h")
    with open(header_file_name, "wt", encoding="utf-8") as f:
        output_copyright(f)

        print("/* This file is automatically generated by create-qr-base.py\n"
              " * DO NOT EDIT\n"
              " */\n",
              file=f)

        base_image = create_base_image()
        base_image.to_pil().save("qr-base.png", "PNG")
        base_image.to_c("base_image", f)

        data_mask_image = create_data_mask_image()
        data_mask_image.to_pil().save("data-mask.png", "PNG")
        data_mask_image.to_c("data_mask_image", f)

        print("static const struct vsx_qr_image\n"
              "mask_images[] = {",
              file=f)

        for i, mask_func in enumerate(MASK_FUNCTIONS):
            mask_image = create_mask_image(data_mask_image, mask_func)
            print("        {", file=f)
            mask_image.nums_to_c(16, f)
            print("        },", file=f)

            mask_image.to_pil().save("mask-{:03b}.png".format(i), "PNG")

        print("};\n", file=f)


if __name__ == '__main__':
    generate_header_file()
