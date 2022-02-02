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

#include "config.h"

#include "vsx-array-object.h"

#include <stdlib.h>
#include <stdint.h>
#include <strings.h>

#include "vsx-gl.h"
#include "vsx-util.h"

#define MAX_ATTRIBUTES 16

struct vsx_array_object_attribute {
        GLint size;
        GLenum type;
        GLboolean normalized;
        GLsizei stride;
        GLuint divisor;
        GLuint buffer;
        size_t buffer_offset;
};

struct vsx_array_object {
        uint32_t enabled_attribs;
        struct vsx_array_object_attribute attributes[MAX_ATTRIBUTES];
        GLuint element_buffer;
};

/* If real VAOs are supported then we'll just stuff the object number
 * in the pointer.
 */
#define VSX_ARRAY_OBJECT_TO_POINTER(x) ((struct vsx_array_object *)     \
                                        (uintptr_t) (x))
#define VSX_ARRAY_OBJECT_FROM_POINTER(x) ((GLuint) (uintptr_t) (x))

struct vsx_array_object *
vsx_array_object_new(struct vsx_gl *gl)
{
        struct vsx_array_object *array;
        GLuint vao;

        if (gl->have_vertex_array_objects) {
                gl->glGenVertexArrays(1, &vao);
                array = VSX_ARRAY_OBJECT_TO_POINTER(vao);
        } else {
                array = vsx_alloc(sizeof *array);
                array->enabled_attribs = 0;
                array->element_buffer = 0;
        }

        return array;
}

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
                               size_t buffer_offset)
{
        GLuint vao;

        if (gl->have_vertex_array_objects) {
                vao = VSX_ARRAY_OBJECT_FROM_POINTER(array);
                gl->glBindVertexArray(vao);
                gl->glBindBuffer(GL_ARRAY_BUFFER, buffer);
                gl->glVertexAttribPointer(index,
                                          size,
                                          type,
                                          normalized,
                                          stride,
                                          (void *) (intptr_t) buffer_offset);
                if (divisor)
                        gl->glVertexAttribDivisor(index, divisor);
                gl->glEnableVertexAttribArray(index);
        } else {
                array->enabled_attribs |= 1 << index;

                array->attributes[index].size = size;
                array->attributes[index].type = type;
                array->attributes[index].normalized = normalized;
                array->attributes[index].stride = stride;
                array->attributes[index].divisor = divisor;
                array->attributes[index].buffer = buffer;
                array->attributes[index].buffer_offset = buffer_offset;
        }
}

void
vsx_array_object_set_element_buffer(struct vsx_array_object *array,
                                    struct vsx_gl *gl,
                                    GLuint buffer)
{
        if (gl->have_vertex_array_objects)
                gl->glBindVertexArray(VSX_ARRAY_OBJECT_FROM_POINTER(array));
        else
                array->element_buffer = buffer;

        /* We bind the buffer immediately even if VAOs aren't
         * available so that the callee can assume it's bound and fill
         * it with data.
         */
        gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer);
}

void
vsx_array_object_bind(struct vsx_array_object *array,
                      struct vsx_gl *gl)
{
        const struct vsx_array_object_attribute *attrib;
        GLuint last_buffer;
        uint32_t attribs;
        int index;

        if (gl->have_vertex_array_objects) {
                gl->glBindVertexArray(VSX_ARRAY_OBJECT_FROM_POINTER(array));
                return;
        }

        last_buffer = ~(GLuint) 0;
        attribs = array->enabled_attribs;

        while ((index = ffs(attribs))) {
                index--;
                attrib = array->attributes + index;
                attribs &= ~(1 << index);

                if (last_buffer != attrib->buffer) {
                        last_buffer = attrib->buffer;
                        gl->glBindBuffer(GL_ARRAY_BUFFER, last_buffer);
                }

                gl->glVertexAttribPointer(index,
                                          attrib->size,
                                          attrib->type,
                                          attrib->normalized,
                                          attrib->stride,
                                          (GLvoid *) (intptr_t)
                                          attrib->buffer_offset);

                if (gl->have_instanced_arrays)
                        gl->glVertexAttribDivisor(index, attrib->divisor);
        }

        attribs = array->enabled_attribs ^ gl->enabled_attribs;

        while ((index = ffs(attribs))) {
                index--;
                attribs &= ~(1 << index);

                if (array->enabled_attribs & (1 << index))
                        gl->glEnableVertexAttribArray(index);
                else
                        gl->glDisableVertexAttribArray(index);
        }

        gl->enabled_attribs = array->enabled_attribs;

        if (array->element_buffer)
                gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                                 array->element_buffer);
}

void
vsx_array_object_free(struct vsx_array_object *array,
                      struct vsx_gl *gl)
{
        GLuint vao;

        if (gl->have_vertex_array_objects) {
                vao = VSX_ARRAY_OBJECT_FROM_POINTER(array);
                gl->glDeleteVertexArrays(1, &vao);
        } else {
                vsx_free(array);
        }
}
