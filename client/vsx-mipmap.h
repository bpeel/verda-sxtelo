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

#ifndef VSX_MIPMAP_H
#define VSX_MIPMAP_H

#include "vsx-error.h"
#include "vsx-gl.h"
#include "vsx-image.h"

void
vsx_mipmap_create_texture_storage(struct vsx_gl *gl,
                                  GLenum format,
                                  GLenum type,
                                  int width,
                                  int height);

void
vsx_mipmap_get_actual_image_size(const struct vsx_image *image,
                                 int *width_out,
                                 int *height_out);

void
vsx_mipmap_load_image(const struct vsx_image *image,
                      struct vsx_gl *gl,
                      GLuint tex);

void
vsx_mipmap_load_image_at_offset(const struct vsx_image *image,
                                struct vsx_gl *gl,
                                GLuint tex,
                                int x_off,
                                int y_off);

#endif /* VSX_MIPMAP_H */
