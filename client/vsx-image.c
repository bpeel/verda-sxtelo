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

#include "config.h"

#include "vsx-image.h"

#define STB_IMAGE_IMPLEMENTATION 1
#define STBI_NO_HDR
#define STBI_NO_STDIO
#include "stb_image.h"

struct vsx_error_domain
vsx_image_error;

bool
vsx_image_load_asset_with_size(struct vsx_image *image,
                               struct vsx_asset *asset,
                               size_t asset_size,
                               struct vsx_error **error)
{
        uint8_t *file_buf = vsx_alloc(asset_size);

        if (!vsx_asset_read(asset, file_buf, asset_size, error)) {
                vsx_free(file_buf);
                return false;
        }

        image->data = stbi_load_from_memory(file_buf,
                                            asset_size,
                                            &image->width,
                                            &image->height,
                                            &image->components,
                                            0 /* req_comp */);

        vsx_free(file_buf);

        if (image->data == NULL) {
                vsx_set_error(error,
                              &vsx_image_error,
                              VSX_IMAGE_ERROR_BAD,
                              "Error loading image");
                return false;
        }

        return true;
}

void
vsx_image_destroy(struct vsx_image *image)
{
        stbi_image_free(image->data);
}
