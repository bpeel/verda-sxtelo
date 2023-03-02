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

#ifndef VSX_QUAD_TOOL_H
#define VSX_QUAD_TOOL_H

#include "vsx-gl.h"
#include "vsx-array-object.h"
#include "vsx-map-buffer.h"

struct vsx_quad_tool;

struct vsx_quad_tool_buffer {
        int ref_count;
        GLuint buf;
        GLenum type;
};

struct vsx_quad_tool *
vsx_quad_tool_new(struct vsx_gl *gl,
                  struct vsx_map_buffer *map_buffer);

struct vsx_quad_tool_buffer *
vsx_quad_tool_get_buffer(struct vsx_quad_tool *tool,
                         struct vsx_array_object *vao,
                         int n_quads);

void
vsx_quad_tool_unref_buffer(struct vsx_quad_tool_buffer *buffer,
                           struct vsx_gl *gl);

void
vsx_quad_tool_free(struct vsx_quad_tool *tool);

#endif /* VSX_QUAD_TOOL_H */
