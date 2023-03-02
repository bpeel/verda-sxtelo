/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2021, 2022  Neil Roberts
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

#include "vsx-quad-tool.h"

#include <assert.h>
#include <stdint.h>

#include "vsx-gl.h"
#include "vsx-map-buffer.h"
#include "vsx-util.h"

struct vsx_quad_tool {
        struct vsx_gl *gl;
        struct vsx_map_buffer *map_buffer;
        struct vsx_quad_tool_buffer *byte_buffer;
        struct vsx_quad_tool_buffer *short_buffer;
        size_t short_buffer_n_quads;
};

struct vsx_quad_tool *
vsx_quad_tool_new(struct vsx_gl *gl,
                  struct vsx_map_buffer *map_buffer)
{
        struct vsx_quad_tool *tool = vsx_calloc(sizeof *tool);

        tool->gl = gl;
        tool->map_buffer = map_buffer;

        return tool;
}

static struct vsx_quad_tool_buffer *
create_buffer(struct vsx_gl *gl,
              struct vsx_array_object *vao,
              GLenum type)
{
        struct vsx_quad_tool_buffer *buffer = vsx_alloc(sizeof *buffer);

        gl->glGenBuffers(1, &buffer->buf);

        vsx_array_object_set_element_buffer(vao, gl, buffer->buf);

        buffer->type = type;
        buffer->ref_count = 1;

        return buffer;
}

static void
generate_byte_buffer(struct vsx_quad_tool *tool,
                     struct vsx_array_object *vao)
{
        struct vsx_gl *gl = tool->gl;

        tool->byte_buffer = create_buffer(gl, vao, GL_UNSIGNED_BYTE);

        size_t n_quads = 256 / 4;
        size_t n_elements = n_quads * 6;

        gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         n_elements, /* buffer_size */
                         NULL, /* data */
                         GL_STATIC_DRAW);

        uint8_t *elements =
                vsx_map_buffer_map(tool->map_buffer,
                                   GL_ELEMENT_ARRAY_BUFFER,
                                   n_elements, /* buffer_size */
                                   false, /* flush_explicit */
                                   GL_STATIC_DRAW);

        uint8_t *e = elements;

        for (unsigned i = 0; i < n_quads; i++) {
                *(e++) = i * 4 + 0;
                *(e++) = i * 4 + 1;
                *(e++) = i * 4 + 2;
                *(e++) = i * 4 + 2;
                *(e++) = i * 4 + 1;
                *(e++) = i * 4 + 3;
        }

        assert(e - elements == n_elements);

        vsx_map_buffer_unmap(tool->map_buffer);
}

static void
generate_short_buffer(struct vsx_quad_tool *tool,
                      struct vsx_array_object *vao,
                      size_t n_quads)
{
        struct vsx_gl *gl = tool->gl;

        struct vsx_quad_tool_buffer *buffer =
                create_buffer(gl, vao, GL_UNSIGNED_SHORT);

        /* For every 4 vertices we want to generate 6 elements to make
         * 2 triangles.
         */
        size_t n_elements = n_quads * 6;
        size_t buffer_size = n_elements * sizeof (uint16_t);

        gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         buffer_size,
                         NULL, /* data */
                         GL_STATIC_DRAW);

        uint16_t *elements =
                vsx_map_buffer_map(tool->map_buffer,
                                   GL_ELEMENT_ARRAY_BUFFER,
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

        vsx_map_buffer_unmap(tool->map_buffer);

        tool->short_buffer = buffer;
}

static struct vsx_quad_tool_buffer *
get_byte_buffer(struct vsx_quad_tool *tool,
                struct vsx_array_object *vao)
{
        if (tool->byte_buffer == NULL) {
                generate_byte_buffer(tool, vao);
        } else {
                vsx_array_object_set_element_buffer(vao,
                                                    tool->gl,
                                                    tool->byte_buffer->buf);
        }

        tool->byte_buffer->ref_count++;

        return tool->byte_buffer;
}

static struct vsx_quad_tool_buffer *
get_short_buffer(struct vsx_quad_tool *tool,
                 struct vsx_array_object *vao,
                 size_t n_quads)
{
        if (tool->short_buffer_n_quads < n_quads) {
                if (tool->short_buffer) {
                        vsx_quad_tool_unref_buffer(tool->short_buffer,
                                                   tool->gl);
                }

                if (tool->short_buffer_n_quads == 0)
                        tool->short_buffer_n_quads = 1;

                while (tool->short_buffer_n_quads < n_quads)
                        tool->short_buffer_n_quads *= 2;

                generate_short_buffer(tool,
                                      vao,
                                      tool->short_buffer_n_quads);
        } else {
                vsx_array_object_set_element_buffer(vao,
                                                    tool->gl,
                                                    tool->short_buffer->buf);
        }

        tool->short_buffer->ref_count++;

        return tool->short_buffer;
}

struct vsx_quad_tool_buffer *
vsx_quad_tool_get_buffer(struct vsx_quad_tool *tool,
                         struct vsx_array_object *vao,
                         int n_quads)
{
        int n_verts = n_quads * 4;

        assert(n_verts > 0 && n_verts - 1 <= UINT16_MAX);

        if (n_verts <= 256)
                return get_byte_buffer(tool, vao);
        else
                return get_short_buffer(tool, vao, n_quads);
}

void
vsx_quad_tool_unref_buffer(struct vsx_quad_tool_buffer *buffer,
                           struct vsx_gl *gl)
{
        assert(buffer->ref_count > 0);

        if (--buffer->ref_count <= 0) {
                gl->glDeleteBuffers(1, &buffer->buf);

                vsx_free(buffer);
        }
}

void
vsx_quad_tool_free(struct vsx_quad_tool *tool)
{
        if (tool->byte_buffer)
                vsx_quad_tool_unref_buffer(tool->byte_buffer, tool->gl);

        if (tool->short_buffer)
                vsx_quad_tool_unref_buffer(tool->short_buffer, tool->gl);

        vsx_free(tool);
}
