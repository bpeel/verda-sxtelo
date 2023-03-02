/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2014, 2021  Neil Roberts
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

#ifndef VSX_GL_H
#define VSX_GL_H

#ifdef __APPLE__
#include <OpenGLES/ES3/gl.h>
#else
#include <GLES3/gl3.h>
#endif
#include <stdbool.h>
#include <stdint.h>

#ifndef GL_APIENTRYP
#define GL_APIENTRYP GL_APIENTRY *
#endif

struct vsx_gl {
#define VSX_GL_BEGIN_GROUP(a, b, c)
#define VSX_GL_FUNC(return_type, name, args) \
        return_type (GL_APIENTRYP name) args;
#define VSX_GL_END_GROUP()
#include "vsx-gl-funcs.h"
#undef VSX_GL_BEGIN_GROUP
#undef VSX_GL_FUNC
#undef VSX_GL_END_GROUP

        int major_version;
        int minor_version;

        bool have_map_buffer_range;
        bool have_vertex_array_objects;

        /* Bitmask of vertex attributes that are currently enabled.
         * This is used to implement the fallback if vertex attribute
         * objects are not available.
         */
        uint32_t enabled_attribs;
};

typedef void *
(* vsx_gl_get_proc_address_func)(char const *procname,
                                 void *user_data);

struct vsx_gl *
vsx_gl_new(vsx_gl_get_proc_address_func get_proc_address_func,
           void *get_proc_address_data);

static inline void
vsx_gl_draw_range_elements(struct vsx_gl *gl,
                           GLenum mode,
                           GLuint start, GLuint end,
                           GLsizei count,
                           GLenum type,
                           const GLvoid *indices)
{
        if (gl->glDrawRangeElements) {
                gl->glDrawRangeElements(mode,
                                        start, end,
                                        count,
                                        type,
                                        indices);
        } else {
                gl->glDrawElements(mode,
                                   count,
                                   type,
                                   indices);
        }
}

void
vsx_gl_free(struct vsx_gl *gl);

#endif /* VSX_GL_H */
