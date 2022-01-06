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

#include <stdlib.h>
#include <stdio.h>

#include "vsx-qr.h"

int
main(int argc, char **argv)
{
        int ret = EXIT_SUCCESS;

        uint8_t buf[VSX_QR_DATA_SIZE] = { 0 };

        fread(buf, 1, sizeof buf, stdin);

        uint8_t image[VSX_QR_IMAGE_SIZE * VSX_QR_IMAGE_SIZE];

        vsx_qr_create(buf, image);

        printf("P5\n"
               "%i %i\n"
               "255\n",
               VSX_QR_IMAGE_SIZE,
               VSX_QR_IMAGE_SIZE);

        for (int y = 0; y < VSX_QR_IMAGE_SIZE; y++) {
                for (int x = 0; x < VSX_QR_IMAGE_SIZE; x++)
                        fputc(image[y * VSX_QR_IMAGE_SIZE + x], stdout);
        }

        return ret;
}
