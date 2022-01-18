/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2014, 2021, 2022  Neil Roberts
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

#include "vsx-shader-data.h"

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include "vsx-util.h"
#include "vsx-buffer.h"
#include "vsx-gl.h"

struct vsx_error_domain
vsx_shader_data_error;

struct vsx_shader_data_shader {
        GLenum type;
        const char **filenames;
        enum vsx_shader_data_program programs[VSX_SHADER_DATA_N_PROGRAMS + 1];
};

#define PROGRAMS_END VSX_SHADER_DATA_N_PROGRAMS

static const struct vsx_shader_data_shader
vsx_shader_data_shaders[] = {
        {
                GL_FRAGMENT_SHADER,
                (const char *[]) { "vsx-texture-fragment.glsl", NULL },
                { VSX_SHADER_DATA_PROGRAM_TEXTURE, PROGRAMS_END }
        },
        {
                GL_VERTEX_SHADER,
                (const char *[]) { "vsx-texture-vertex.glsl", NULL },
                {
                        VSX_SHADER_DATA_PROGRAM_TEXTURE,
                        VSX_SHADER_DATA_PROGRAM_LAYOUT,
                        PROGRAMS_END
                }
        },
        {
                GL_FRAGMENT_SHADER,
                (const char *[]) { "vsx-layout-fragment.glsl", NULL },
                { VSX_SHADER_DATA_PROGRAM_LAYOUT, PROGRAMS_END }
        },
        {
                GL_FRAGMENT_SHADER,
                (const char *[]) { "vsx-solid-fragment.glsl", NULL },
                { VSX_SHADER_DATA_PROGRAM_SOLID, PROGRAMS_END }
        },
        {
                GL_VERTEX_SHADER,
                (const char *[]) { "vsx-solid-vertex.glsl", NULL },
                { VSX_SHADER_DATA_PROGRAM_SOLID, PROGRAMS_END }
        },
};

static GLuint
create_shader(const char *name,
              GLenum type,
              const char *source,
              int source_length,
              struct vsx_error **error)
{
        GLuint shader;
        GLint length, compile_status;
        GLsizei actual_length;
        GLchar *info_log;
        const char *source_strings[1];
        GLint lengths[VSX_N_ELEMENTS(source_strings)];
        int n_strings = 0;

        shader = vsx_gl.glCreateShader(type);

        source_strings[n_strings] = source;
        lengths[n_strings++] = source_length;
        vsx_gl.glShaderSource(shader,
                              n_strings,
                              (const GLchar **) source_strings,
                              lengths);

        vsx_gl.glCompileShader(shader);

        vsx_gl.glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);

        if (length > 0) {
                info_log = malloc(length);
                vsx_gl.glGetShaderInfoLog(shader, length,
                                          &actual_length,
                                          info_log);
                if (*info_log) {
                        fprintf(stderr,
                                "Info log for %s:\n%s\n",
                                name, info_log);
                }
                free(info_log);
        }

        vsx_gl.glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);

        if (!compile_status) {
                vsx_set_error(error,
                              &vsx_shader_data_error,
                              VSX_SHADER_DATA_ERROR_COMPILATION_FAILED,
                              "%s compilation failed",
                              name);
                vsx_gl.glDeleteShader(shader);
                return 0;
        }

        return shader;
}

static GLuint
create_shader_from_files(struct vsx_asset_manager *asset_manager,
                         GLenum shader_type,
                         const char **filenames,
                         struct vsx_error **error)
{
        char *source, *p;
        size_t *lengths;
        long int total_length = 0;
        GLuint shader;
        int n_files;
        struct vsx_asset **assets;
        int i;

        for (n_files = 0, i = 0; filenames[i]; i++)
                n_files++;

        assets = alloca(sizeof assets[0] * n_files);
        lengths = alloca(sizeof lengths[0] * n_files);

        for (n_files = 0, i = 0; filenames[i]; i++) {
                assets[i] = vsx_asset_manager_open(asset_manager,
                                                   filenames[i],
                                                   error);

                if (assets[i] == NULL) {
                        shader = 0;
                        goto out;
                }

                n_files++;

                if (!vsx_asset_remaining(assets[i], lengths + i, error)) {
                        shader = 0;
                        goto out;
                }

                total_length += lengths[i];
        }

        source = vsx_alloc(total_length + 1);

        for (p = source, i = 0; i < n_files; i++) {
                if (!vsx_asset_read(assets[i], p, lengths[i], error)) {
                        shader = 0;
                        vsx_free(source);
                        goto out;
                }

                p += lengths[i];
        }

        /* Emscripten's version of glShaderSource seems to ignore the
         * length and interpret the string as null-terminated.
         */
        source[total_length] = '\0';

        shader = create_shader(filenames[n_files - 1],
                               shader_type,
                               source,
                               total_length,
                               error);

        vsx_free(source);

        goto out;

out:
        for (i = 0; i < n_files; i++)
                vsx_asset_close(assets[i]);

        return shader;
}

static bool
shader_contains_program(const struct vsx_shader_data_shader *shader,
                        enum vsx_shader_data_program program_num)
{
        int i;

        for (i = 0; shader->programs[i] != PROGRAMS_END; i++) {
                if (shader->programs[i] == program_num)
                        return true;
        }

        return false;
}

static char *
get_program_name(enum vsx_shader_data_program program_num)
{
        struct vsx_buffer buffer = VSX_BUFFER_STATIC_INIT;
        const struct vsx_shader_data_shader *shader;
        int i, j;

        /* Generate the program name as just a list of the shaders it
         * contains */

        vsx_buffer_append_c(&buffer, '(');

        for (i = 0; i < VSX_N_ELEMENTS(vsx_shader_data_shaders); i++) {
                shader = vsx_shader_data_shaders + i;

                if (shader_contains_program(shader, program_num)) {
                        if (buffer.length > 1)
                                vsx_buffer_append_string(&buffer, ", ");
                        for (j = 0; shader->filenames[j]; j++)
                                vsx_buffer_append_string(&buffer,
                                                         shader->filenames[j]);
                }
        }

        vsx_buffer_append_string(&buffer, ")");

        return (char *) buffer.data;
}

static void
get_uniforms(struct vsx_shader_data_program_data *program)
{
        program->tex_uniform =
                vsx_gl.glGetUniformLocation(program->program, "tex");

        if (program->tex_uniform != -1) {
                vsx_gl.glUseProgram(program->program);
                vsx_gl.glUniform1i(program->tex_uniform, 0);
        }

        program->matrix_uniform =
                vsx_gl.glGetUniformLocation(program->program,
                                            "transform_matrix");
        program->translation_uniform =
                vsx_gl.glGetUniformLocation(program->program,
                                            "translation");
        program->color_uniform =
                vsx_gl.glGetUniformLocation(program->program,
                                            "color");
}

static bool
link_program(struct vsx_shader_data *data,
             enum vsx_shader_data_program program_num,
             struct vsx_error **error)
{
        GLint length, link_status;
        GLsizei actual_length;
        GLchar *info_log;
        GLuint program;
        char *program_name;

        program = data->programs[program_num].program;

        vsx_gl.glBindAttribLocation(program,
                                    VSX_SHADER_DATA_ATTRIB_POSITION,
                                    "position");
        vsx_gl.glBindAttribLocation(program,
                                    VSX_SHADER_DATA_ATTRIB_TEX_COORD,
                                    "tex_coord_attrib");
        vsx_gl.glBindAttribLocation(program,
                                    VSX_SHADER_DATA_ATTRIB_NORMAL,
                                    "normal_attrib");
        vsx_gl.glBindAttribLocation(program,
                                    VSX_SHADER_DATA_ATTRIB_COLOR,
                                    "color_attrib");

        vsx_gl.glLinkProgram(program);

        vsx_gl.glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);

        if (length > 0) {
                info_log = malloc(length);
                vsx_gl.glGetProgramInfoLog(program, length,
                                           &actual_length,
                                           info_log);
                if (*info_log) {
                        program_name = get_program_name(program_num);
                        fprintf(stderr, "Link info log for %s:\n%s\n",
                                program_name,
                                info_log);
                        vsx_free(program_name);
                }
                free(info_log);
        }

        vsx_gl.glGetProgramiv(program, GL_LINK_STATUS, &link_status);

        if (!link_status) {
                program_name = get_program_name(program_num);
                vsx_set_error(error,
                              &vsx_shader_data_error,
                              VSX_SHADER_DATA_ERROR_LINK_FAILED,
                              "%s program link failed",
                              program_name);
                vsx_free(program_name);
                return false;
        }

        get_uniforms(data->programs + program_num);

        return true;
}

static bool
link_programs(struct vsx_shader_data *data,
              struct vsx_error **error)
{
        int i;

        for (i = 0; i < VSX_SHADER_DATA_N_PROGRAMS; i++) {
                if (!link_program(data, i, error))
                        return false;
        }

        return true;
}

bool
vsx_shader_data_init(struct vsx_shader_data *data,
                     struct vsx_asset_manager *asset_manager,
                     struct vsx_error **error)
{
        const struct vsx_shader_data_shader *shader;
        GLuint shaders[VSX_N_ELEMENTS(vsx_shader_data_shaders)];
        GLuint program;
        bool result = true;
        int n_shaders;
        int i, j;

        for (n_shaders = 0; n_shaders < VSX_N_ELEMENTS(shaders); n_shaders++) {
                shader = vsx_shader_data_shaders + n_shaders;
                shaders[n_shaders] =
                        create_shader_from_files(asset_manager,
                                                 shader->type,
                                                 shader->filenames,
                                                 error);
                if (shaders[n_shaders] == 0) {
                        result = false;
                        goto out;
                }
        }

        for (i = 0; i < VSX_SHADER_DATA_N_PROGRAMS; i++)
                data->programs[i].program = vsx_gl.glCreateProgram();

        for (i = 0; i < VSX_N_ELEMENTS(shaders); i++) {
                shader = vsx_shader_data_shaders + i;
                for (j = 0; shader->programs[j] != PROGRAMS_END; j++) {
                        program = data->programs[shader->programs[j]].program;
                        vsx_gl.glAttachShader(program, shaders[i]);
                }
        }

        if (!link_programs(data, error)) {
                for (i = 0; i < VSX_SHADER_DATA_N_PROGRAMS; i++)
                        vsx_gl.glDeleteProgram(data->programs[i].program);
                result = false;
        }

out:
        for (i = 0; i < n_shaders; i++)
                vsx_gl.glDeleteShader(shaders[i]);

        return result;
}

void
vsx_shader_data_destroy(struct vsx_shader_data *data)
{
        int i;

        for (i = 0; i < VSX_SHADER_DATA_N_PROGRAMS; i++)
                vsx_gl.glDeleteProgram(data->programs[i].program);
}
