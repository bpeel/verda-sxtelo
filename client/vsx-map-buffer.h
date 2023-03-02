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

#ifndef VSX_MAP_BUFFER_H
#define VSX_MAP_BUFFER_H

#include <stdbool.h>

#include "vsx-gl.h"

struct vsx_map_buffer;

struct vsx_map_buffer *
vsx_map_buffer_new(struct vsx_gl *gl);

/* Maps the given buffer target for writing. This will always
 * invalidate the entire buffer contents and it cannot be used to map
 * a subrange. The length parameter should be the length of the entire
 * buffer. It will only be mapped for writing. If mapping is not
 * available or the map fails it will resort to using a temporary
 * buffer which will be copied in when the buffer is unmapped. This
 * can not be used to map multiple buffers simultaneously. The buffer
 * binding state must not be changed while a buffer is mapped.
 */
void *
vsx_map_buffer_map(struct vsx_map_buffer *map_buffer,
                   GLenum target,
                   GLsizeiptr length,
                   bool flush_explicit,
                   GLenum usage);

void
vsx_map_buffer_flush(struct vsx_map_buffer *map_buffer,
                     GLintptr offset,
                     GLsizeiptr length);

void
vsx_map_buffer_unmap(struct vsx_map_buffer *map_buffer);

void
vsx_map_buffer_free(struct vsx_map_buffer *map_buffer);

#endif /* VSX_MAP_BUFFER_H */
