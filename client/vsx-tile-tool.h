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

#ifndef VSX_TILE_TOOL_H
#define VSX_TILE_TOOL_H

#include "vsx-gl.h"
#include "vsx-image-loader.h"
#include "vsx-map-buffer.h"
#include "vsx-quad-tool.h"
#include "vsx-shader-data.h"
#include "vsx-tile-texture.h"
#include "vsx-signal.h"

struct vsx_tile_tool;
struct vsx_tile_tool_buffer;

struct vsx_tile_tool *
vsx_tile_tool_new(struct vsx_gl *gl,
                  struct vsx_image_loader *image_loader,
                  struct vsx_map_buffer *map_buffer,
                  struct vsx_quad_tool *quad_tool);

bool
vsx_tile_tool_is_ready(struct vsx_tile_tool *tool);

struct vsx_signal *
vsx_tile_tool_get_ready_signal(struct vsx_tile_tool *tool);

void
vsx_tile_tool_paint(struct vsx_tile_tool_buffer *buf,
                    const struct vsx_shader_data *shader_data,
                    const GLfloat *matrix,
                    const GLfloat *translation);

struct vsx_tile_tool_buffer *
vsx_tile_tool_create_buffer(struct vsx_tile_tool *tool,
                            int tile_size);

void
vsx_tile_tool_begin_update(struct vsx_tile_tool_buffer *buf,
                           int max_tiles);

void
vsx_tile_tool_add_tile(struct vsx_tile_tool_buffer *buf,
                       int tile_x, int tile_y,
                       const struct vsx_tile_texture_letter *letter_data);

void
vsx_tile_tool_end_update(struct vsx_tile_tool_buffer *buf);

void
vsx_tile_tool_free_buffer(struct vsx_tile_tool_buffer *buf);

void
vsx_tile_tool_free(struct vsx_tile_tool *tool);

#endif /* VSX_TILE_TOOL_H */
