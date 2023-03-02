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


import sys
import tempfile
import subprocess
import PIL.Image


GENERATE_QR_EXE = sys.argv[1]

TESTS = [
    "https://gemelo.org/j/kMJ-D-rsabM"
]


def generate_expected_qr(data, filename):
    with tempfile.NamedTemporaryFile() as datafile:
        datafile.write(data)
        datafile.flush()

        subprocess.check_call(["qrencode",
                               "--level=Q",
                               "--size=1",
                               "--read-from=" + datafile.name,
                               "--output=" + filename])


def images_are_same(image1_fn, image2_fn):
    image1 = PIL.Image.open(image1_fn).convert('1', dither=PIL.Image.NONE)
    image2 = PIL.Image.open(image2_fn).convert('1', dither=PIL.Image.NONE)

    if image1.size != image2.size:
        print("Images have different size ({} != {})".format(
            image1.size, image2.size),
              file=sys.stderr)
        return False

    if list(image1.getdata()) != list(image2.getdata()):
        print("Images are different", file=sys.stderr)
        return False

    return True


def test_data(data):
    with tempfile.NamedTemporaryFile(suffix=".pgm") as generated_qr:
        gen_proc = subprocess.Popen([GENERATE_QR_EXE],
                                    stdin=subprocess.PIPE,
                                    stdout=generated_qr)
        gen_proc.stdin.write(data)
        gen_proc.stdin.close()
        if gen_proc.wait() != 0:
            return False

        with tempfile.NamedTemporaryFile(suffix=".png") as expected_qr:
            generate_expected_qr(data, expected_qr.name)

            if not images_are_same(generated_qr.name, expected_qr.name):
                return False

    return True


ret = True

for test in TESTS:
    if type(test) == str:
        test = test.encode("utf-8")

    if not test_data(test):
        ret = False

if not ret:
    sys.exit(1)
