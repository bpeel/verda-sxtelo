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

#include "vsx-quad-buffer.h"

#include <assert.h>

#include "vsx-map-buffer.h"

GLuint
vsx_quad_buffer_generate(struct vsx_array_object *vao,
                         size_t n_quads)
{
        /* For every 4 vertices we want to generate 6 elements to make
         * 2 triangles.
         */
        size_t n_elements = n_quads * 6;
        size_t buffer_size = n_elements * sizeof (uint16_t);

        GLuint element_buffer;
        vsx_gl.glGenBuffers(1, &element_buffer);

        vsx_array_object_set_element_buffer(vao, &vsx_gl, element_buffer);

        vsx_gl.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                            buffer_size,
                            NULL, /* data */
                            GL_STATIC_DRAW);

        uint16_t *elements =
                vsx_map_buffer_map(GL_ELEMENT_ARRAY_BUFFER,
                                   buffer_size,
                                   false, /* flush_explicit */
                                   GL_STATIC_DRAW);
        uint16_t *e = elements;

        for (unsigned i = 0; i < n_quads; i++) {
                *(e++) = i * 4 + 0;
                *(e++) = i * 4 + 1;
                *(e++) = i * 4 + 2;
                *(e++) = i * 4 + 2;
                *(e++) = i * 4 + 1;
                *(e++) = i * 4 + 3;
        }

        assert((e - elements) == n_elements);

        vsx_map_buffer_unmap();

        return element_buffer;
}
