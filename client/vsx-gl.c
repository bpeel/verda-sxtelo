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

#include "config.h"

#include "vsx-gl.h"

#include <string.h>

#include "vsx-buffer.h"

struct loader_data {
        struct vsx_gl *gl;
        vsx_gl_get_proc_address_func get_proc_address_func;
        void *get_proc_address_data;
        struct vsx_buffer extensions;
};

struct vsx_gl_func {
        const char *name;
        size_t offset;
};

struct vsx_gl_group {
        int minimum_gl_version;
        const char *extension;
        const char *extension_suffix;
        const struct vsx_gl_func *funcs;
};

static const struct vsx_gl_group
gl_groups[] = {
#define VSX_GL_BEGIN_GROUP(min_gl_version, ext, suffix) \
        { .minimum_gl_version = min_gl_version,         \
        .extension = ext,                               \
        .extension_suffix = suffix,                     \
        .funcs = (const struct vsx_gl_func[]) {
#define VSX_GL_FUNC(return_type, func_name, args)                       \
        { .name = #func_name, .offset = offsetof(struct vsx_gl, func_name) },
#define VSX_GL_END_GROUP()                      \
        { .name = NULL }                        \
} },
#include "vsx-gl-funcs.h"
#undef VSX_GL_BEGIN_GROUP
#undef VSX_GL_FUNC
#undef VSX_GL_END_GROUP
};

static void
parse_extensions(struct loader_data *data)
{
        /* Convert the GL_EXTENSIONS string into an easily scannable
         * list of zero-terminated strings.
         */

        const char *exts = (const char *) data->gl->glGetString(GL_EXTENSIONS);

        while (true) {
                while (*exts == ' ')
                        exts++;

                if (*exts == '\0')
                        break;

                const char *end;

                for (end = exts + 1; *end != '\0' && *end != ' '; end++);

                vsx_buffer_append(&data->extensions, exts, end - exts);
                vsx_buffer_append_c(&data->extensions, '\0');

                exts = end;
        }
}

static bool
is_extension_supported(struct loader_data *data,
                       const char *extension_name)
{
        const char *extensions = (const char *) data->extensions.data;
        const char *end = extensions + data->extensions.length;

        while (extensions < end) {
                if (!strcmp(extensions, extension_name))
                        return true;

                extensions += strlen(extensions) + 1;
        }

        return false;
}

static void
get_gl_version(struct vsx_gl *gl)
{
        const char *version_string =
                (const char *) gl->glGetString(GL_VERSION);
        static const char version_string_prefix[] = "OpenGL ES ";
        int major_version = 0;
        int minor_version = 0;

        const char *number_start =
                strstr(version_string, version_string_prefix);
        if (number_start == NULL)
                goto invalid;

        number_start += strlen(version_string_prefix);

        const char *p = number_start;

        while (*p >= '0' && *p <= '9') {
                major_version = major_version * 10 + *p - '0';
                p++;
        }

        if (p == number_start || *p != '.')
                goto invalid;

        p++;

        number_start = p;

        while (*p >= '0' && *p <= '9') {
                minor_version = minor_version * 10 + *p - '0';
                p++;
        }

        if (number_start == p)
                goto invalid;

        gl->major_version = major_version;
        gl->minor_version = minor_version;

        return;

invalid:
        gl->major_version = -1;
        gl->minor_version = -1;
}

static void
init_group(struct loader_data *data,
           const struct vsx_gl_group *group)
{
        struct vsx_gl *gl = data->gl;
        int minor_gl_version = gl->minor_version;
        const char *suffix;
        struct vsx_buffer buffer;
        void *func;
        int gl_version;
        int i;

        if (minor_gl_version >= 10)
                minor_gl_version = 9;
        gl_version = gl->major_version * 10 + minor_gl_version;

        if (group->minimum_gl_version >= 0 &&
            gl_version >= group->minimum_gl_version)
                suffix = "";
        else if (group->extension &&
                 is_extension_supported(data, group->extension))
                suffix = group->extension_suffix;
        else
                return;

        vsx_buffer_init(&buffer);

        for (i = 0; group->funcs[i].name; i++) {
                vsx_buffer_set_length(&buffer, 0);
                vsx_buffer_append_string(&buffer, group->funcs[i].name);
                vsx_buffer_append_string(&buffer, suffix);
                func = data->get_proc_address_func((char *) buffer.data,
                                                   data->get_proc_address_data);
                *(void **) ((char *) gl + group->funcs[i].offset) = func;
        }

        vsx_buffer_destroy(&buffer);
}

struct vsx_gl *
vsx_gl_new(vsx_gl_get_proc_address_func get_proc_address_func,
           void *get_proc_address_data)
{
        struct loader_data data = {
                .gl = vsx_calloc(sizeof (struct vsx_gl)),
                .get_proc_address_func = get_proc_address_func,
                .get_proc_address_data = get_proc_address_data,
                .extensions = VSX_BUFFER_STATIC_INIT,
        };

        data.gl->glGetString =
                get_proc_address_func("glGetString",
                                      get_proc_address_data);

        parse_extensions(&data);

        get_gl_version(data.gl);

        for (int i = 0; i < VSX_N_ELEMENTS(gl_groups); i++)
                init_group(&data, gl_groups + i);

        data.gl->have_map_buffer_range = data.gl->glMapBufferRange != NULL;
        data.gl->have_vertex_array_objects = data.gl->glGenVertexArrays != NULL;

        int max_vertex_attribs;

        data.gl->glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &max_vertex_attribs);

        data.gl->have_instanced_arrays =
                data.gl->glVertexAttribDivisor != NULL &&
                data.gl->glDrawElementsInstanced != NULL &&
                max_vertex_attribs >= 11;

        vsx_buffer_destroy(&data.extensions);

        return data.gl;
}

void
vsx_gl_free(struct vsx_gl *gl)
{
        vsx_free(gl);
}
