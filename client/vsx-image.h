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

#ifndef VSX_IMAGE_H
#define VSX_IMAGE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "vsx-error.h"
#include "vsx-asset.h"

extern struct vsx_error_domain
vsx_image_error;

struct vsx_image {
        int width, height;
        int components;
        uint8_t *data;
};

enum vsx_image_error {
        VSX_IMAGE_ERROR_BAD,
};

bool
vsx_image_load_asset_with_size(struct vsx_image *image,
                               struct vsx_asset *asset,
                               size_t asset_size,
                               struct vsx_error **error);

void
vsx_image_destroy(struct vsx_image *image);

#endif /* VSX_IMAGE_H */
