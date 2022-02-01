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

#include "vsx-mipmap.h"

#include <string.h>

#include "vsx-gl.h"
#include "vsx-util.h"
#include "vsx-image.h"
#include "vsx-asset.h"

void
vsx_mipmap_get_actual_image_size(const struct vsx_image *image,
                                 int *width_out,
                                 int *height_out)
{
        *width_out = image->width;
        /* The image in the file is 1.5 times the size of the base
         * image in order to accomodate the mipmap images.
         */
        *height_out = image->height * 2 / 3;
}

void
vsx_mipmap_create_texture_storage(struct vsx_gl *gl,
                                  GLenum format,
                                  GLenum type,
                                  int width,
                                  int height)
{
        int mipmap_level = 0;

        while (true) {
                gl->glTexImage2D(GL_TEXTURE_2D,
                                 mipmap_level,
                                 format,
                                 width, height,
                                 0, /* border */
                                 format,
                                 type,
                                 NULL /* data */);

                if (width <= 1 && height <= 1)
                        break;

                width = MAX(1, width / 2);
                height = MAX(1, height / 2);
                mipmap_level++;
        }
}

static void
copy_image(uint8_t *dst,
           const uint8_t *src,
           int width,
           int height,
           int components,
           int src_stride)
{
        int dst_stride = ((width * components) + 3) & ~3;

        for (int y = 0; y < height; y++) {
                memcpy(dst, src, dst_stride);
                dst += dst_stride;
                src += src_stride;
        }
}

static GLenum
format_for_image(const struct vsx_image *image)
{
        switch (image->components) {
        case 4:
                return GL_RGBA;
        case 3:
                return GL_RGB;
        case 2:
                return GL_LUMINANCE_ALPHA;
        case 1:
                return GL_ALPHA;
        }
}

void
vsx_mipmap_load_image(const struct vsx_image *image,
                      struct vsx_gl *gl,
                      GLuint tex)
{
        gl->glBindTexture(GL_TEXTURE_2D, tex);

        int width, height;
        vsx_mipmap_get_actual_image_size(image, &width, &height);

        vsx_mipmap_create_texture_storage(gl,
                                          format_for_image(image),
                                          GL_UNSIGNED_BYTE,
                                          width, height);

        vsx_mipmap_load_image_at_offset(image, gl, tex, 0, 0);
}

void
vsx_mipmap_load_image_at_offset(const struct vsx_image *image,
                                struct vsx_gl *gl,
                                GLuint tex,
                                int x_off,
                                int y_off)
{
        gl->glBindTexture(GL_TEXTURE_2D, tex);

        bool go_down = true;
        int mipmap_level = 0;
        GLint format = format_for_image(image);
        int image_stride = image->width * image->components;
        int x = 0, y = 0;

        int width, height;
        vsx_mipmap_get_actual_image_size(image, &width, &height);

        while (true) {
                /* We can’t upload a subregion of an image with GLES
                 * so let’s copy it into the top of the buffer without
                 * any padding between the lines.
                 */
                if (mipmap_level > 0) {
                        copy_image(image->data,
                                   image->data +
                                   x * image->components +
                                   y * image_stride,
                                   width, height,
                                   image->components,
                                   image_stride);
                }

                gl->glTexSubImage2D(GL_TEXTURE_2D,
                                    mipmap_level,
                                    x_off, y_off,
                                    width, height,
                                    format,
                                    GL_UNSIGNED_BYTE,
                                    image->data);

                if (width <= 1 && height <= 1)
                        break;

                mipmap_level++;

                if (go_down) {
                        y += height;
                        go_down = false;
                } else {
                        x += width;
                        go_down = true;
                }

                if (width > 1)
                        width /= 2;
                if (height > 1)
                        height /= 2;
                x_off /= 2;
                y_off /= 2;
        }
}
