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

#include "vsx-map-buffer.h"

#include "vsx-gl.h"
#include "vsx-buffer.h"

struct vsx_map_buffer {
        struct vsx_gl *gl;
        GLenum target;
        GLenum usage;
        GLsizeiptr length;
        bool flush_explicit;
        bool using_buffer;
        struct vsx_buffer buffer;
};

struct vsx_map_buffer *
vsx_map_buffer_new(struct vsx_gl *gl)
{
        struct vsx_map_buffer *map_buffer = vsx_calloc(sizeof *map_buffer);

        map_buffer->gl = gl;
        vsx_buffer_init(&map_buffer->buffer);

        return map_buffer;
}

void *
vsx_map_buffer_map(struct vsx_map_buffer *map_buffer,
                   GLenum target,
                   GLsizeiptr length,
                   bool flush_explicit,
                   GLenum usage)
{
        GLbitfield flags;
        void *ret = NULL;

        map_buffer->target = target;
        map_buffer->usage = usage;
        map_buffer->length = length;
        map_buffer->flush_explicit = flush_explicit;

        struct vsx_gl *gl = map_buffer->gl;

        if (gl->have_map_buffer_range) {
                flags = (GL_MAP_WRITE_BIT |
                         GL_MAP_INVALIDATE_BUFFER_BIT);
                if (flush_explicit)
                        flags |= GL_MAP_FLUSH_EXPLICIT_BIT;
                ret = gl->glMapBufferRange(target,
                                           0, /* offset */
                                           length,
                                           flags);
                if (ret) {
                        map_buffer->using_buffer = false;
                        return ret;
                }
        }

        map_buffer->using_buffer = true;

        vsx_buffer_set_length(&map_buffer->buffer, length);

        if (flush_explicit) {
                /* Reset the data to NULL so that the GL driver can
                 * know that it doesn't need to preserve the old
                 * contents if only a subregion is flushed.
                 */
                gl->glBufferData(target, length, NULL, usage);
        }

        return map_buffer->buffer.data;
}

void
vsx_map_buffer_flush(struct vsx_map_buffer *map_buffer,
                     GLintptr offset,
                     GLsizeiptr length)
{
        struct vsx_gl *gl = map_buffer->gl;

        if (map_buffer->using_buffer) {
                gl->glBufferSubData(map_buffer->target,
                                    offset,
                                    length,
                                    map_buffer->buffer.data +
                                    offset);
        } else {
                gl->glFlushMappedBufferRange(map_buffer->target,
                                             offset,
                                             length);
        }
}

void
vsx_map_buffer_unmap(struct vsx_map_buffer *map_buffer)
{
        struct vsx_gl *gl = map_buffer->gl;

        if (map_buffer->using_buffer) {
                if (!map_buffer->flush_explicit) {
                        gl->glBufferData(map_buffer->target,
                                         map_buffer->length,
                                         map_buffer->buffer.data,
                                         map_buffer->usage);
                }
        } else {
                gl->glUnmapBuffer(map_buffer->target);
        }
}

void
vsx_map_buffer_free(struct vsx_map_buffer *map_buffer)
{
        vsx_buffer_destroy(&map_buffer->buffer);
        vsx_free(map_buffer);
}
