/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2015, 2021  Neil Roberts
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

#ifndef VSX_ARRAY_OBJECT_H
#define VSX_ARRAY_OBJECT_H

#include <stdlib.h>

#include "vsx-gl.h"

struct vsx_array_object;

struct vsx_array_object *
vsx_array_object_new(struct vsx_gl *gl);

void
vsx_array_object_set_attribute(struct vsx_array_object *array,
                               struct vsx_gl *gl,
                               GLuint index,
                               GLint size,
                               GLenum type,
                               GLboolean normalized,
                               GLsizei stride,
                               GLuint divisor,
                               GLuint buffer,
                               size_t buffer_offset);

/* Sets the element buffer for the array object. Note that this will
 * also end up binding the element buffer so that it can be
 * immediately filled with data.
 */
void
vsx_array_object_set_element_buffer(struct vsx_array_object *array,
                                    struct vsx_gl *gl,
                                    GLuint buffer);

void
vsx_array_object_bind(struct vsx_array_object *array,
                      struct vsx_gl *gl);

void
vsx_array_object_free(struct vsx_array_object *array,
                      struct vsx_gl *gl);

#endif /* VSX_ARRAY_OBJECT_H */
