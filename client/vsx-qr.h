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

#ifndef VSX_QR_H
#define VSX_QR_H

#include <stdint.h>

/* The size in pixels of each axis of the resulting image */
#define VSX_QR_IMAGE_SIZE 37

/* The QR image encodes exactly this number of bytes of information */
#define VSX_QR_DATA_SIZE 32

void
vsx_qr_create(const uint8_t *data,
              uint8_t *image);

#endif /* VSX_QR_H */
