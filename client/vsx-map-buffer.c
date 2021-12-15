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

static struct
{
        GLenum target;
        GLenum usage;
        GLsizeiptr length;
        bool flush_explicit;
        bool using_buffer;
        struct vsx_buffer buffer;
} vsx_map_buffer_state = {
        .buffer = VSX_BUFFER_STATIC_INIT
};

void *
vsx_map_buffer_map(GLenum target,
                   GLsizeiptr length,
                   bool flush_explicit,
                   GLenum usage)
{
        GLbitfield flags;
        void *ret = NULL;

        vsx_map_buffer_state.target = target;
        vsx_map_buffer_state.usage = usage;
        vsx_map_buffer_state.length = length;
        vsx_map_buffer_state.flush_explicit = flush_explicit;

        if (vsx_gl.have_map_buffer_range) {
                flags = (GL_MAP_WRITE_BIT |
                         GL_MAP_INVALIDATE_BUFFER_BIT);
                if (flush_explicit)
                        flags |= GL_MAP_FLUSH_EXPLICIT_BIT;
                ret = vsx_gl.glMapBufferRange(target,
                                              0, /* offset */
                                              length,
                                              flags);
                if (ret) {
                        vsx_map_buffer_state.using_buffer = false;
                        return ret;
                }
        }

        vsx_map_buffer_state.using_buffer = true;

        vsx_buffer_set_length(&vsx_map_buffer_state.buffer, length);

        if (flush_explicit) {
                /* Reset the data to NULL so that the GL driver can
                 * know that it doesn't need to preserve the old
                 * contents if only a subregion is flushed.
                 */
                vsx_gl.glBufferData(target, length, NULL, usage);
        }

        return vsx_map_buffer_state.buffer.data;
}

void
vsx_map_buffer_flush(GLintptr offset,
                     GLsizeiptr length)
{
        if (vsx_map_buffer_state.using_buffer) {
                vsx_gl.glBufferSubData(vsx_map_buffer_state.target,
                                       offset,
                                       length,
                                       vsx_map_buffer_state.buffer.data +
                                       offset);
        } else {
                vsx_gl.glFlushMappedBufferRange(vsx_map_buffer_state.target,
                                                offset,
                                                length);
        }
}

void
vsx_map_buffer_unmap(void)
{
        if (vsx_map_buffer_state.using_buffer) {
                if (!vsx_map_buffer_state.flush_explicit)
                        vsx_gl.glBufferData(vsx_map_buffer_state.target,
                                            vsx_map_buffer_state.length,
                                            vsx_map_buffer_state.buffer.data,
                                            vsx_map_buffer_state.usage);
        } else {
                vsx_gl.glUnmapBuffer(vsx_map_buffer_state.target);
        }
}
