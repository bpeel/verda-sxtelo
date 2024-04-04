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

#include "vsx-tile-tool.h"

#include <assert.h>
#include <string.h>

#include "vsx-mipmap.h"
#include "vsx-board.h"
#include "vsx-array-object.h"
#include "vsx-buffer.h"

struct draw_call {
        int n_tiles;
        int texture;
};

struct vsx_tile_tool_buffer {
        struct vsx_tile_tool *tool;

        struct vsx_array_object *vao;
        GLuint vbo;
        struct vsx_quad_tool_buffer *quad_buffer;

        struct vertex *vertices, *v;

        struct vsx_buffer draw_calls;

        int max_tiles;
        int tile_size;
};

struct vsx_tile_tool {
        struct vsx_gl *gl;
        struct vsx_shell_interface *shell;
        struct vsx_image_loader *image_loader;
        struct vsx_map_buffer *map_buffer;
        struct vsx_quad_tool *quad_tool;

        GLuint textures[VSX_TILE_TEXTURE_N_TEXTURES];
        struct vsx_image_loader_token *image_token;

        struct vsx_signal ready_signal;
};

struct vertex {
        float x, y;
        uint16_t s, t;
};

static void
texture_load_cb(const struct vsx_image *image,
                struct vsx_error *error,
                void *data);

static void
start_texture_load(struct vsx_tile_tool *tool,
                   int texture_num)
{
        assert(tool->image_token == NULL);

        struct vsx_buffer buf = VSX_BUFFER_STATIC_INIT;

        vsx_buffer_append_printf(&buf, "tiles-%i.mpng", texture_num);

        tool->image_token = vsx_image_loader_load(tool->image_loader,
                                                  (const char *) buf.data,
                                                  texture_load_cb,
                                                  tool);

        vsx_buffer_destroy(&buf);
}

static void
texture_load_cb(const struct vsx_image *image,
                struct vsx_error *error,
                void *data)
{
        struct vsx_tile_tool *tool = data;

        tool->image_token = NULL;

        if (error) {
                struct vsx_shell_interface *shell = tool->shell;

                shell->log_error_cb(shell,
                                    "error loading tiles image: %s",
                                    error->message);

                return;
        }

        struct vsx_gl *gl = tool->gl;

        GLuint tex;

        gl->glGenTextures(1, &tex);

        gl->glBindTexture(GL_TEXTURE_2D, tex);
        gl->glTexParameteri(GL_TEXTURE_2D,
                            GL_TEXTURE_WRAP_S,
                            GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D,
                            GL_TEXTURE_WRAP_T,
                            GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D,
                            GL_TEXTURE_MIN_FILTER,
                            GL_LINEAR_MIPMAP_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D,
                            GL_TEXTURE_MAG_FILTER,
                            GL_LINEAR);

        vsx_mipmap_load_image(image, gl, tex);

        for (int i = 0; i < VSX_TILE_TEXTURE_N_TEXTURES; i++) {
                if (tool->textures[i] == 0) {
                        tool->textures[i] = tex;

                        if (i >= VSX_TILE_TEXTURE_N_TEXTURES - 1) {
                                vsx_signal_emit(&tool->ready_signal, NULL);
                        } else {
                                start_texture_load(tool, i + 1);
                        }

                        goto found_empty_texture;
                }
        }

        assert(!"extra tile texture loaded!");

found_empty_texture: (void) 0;
}

struct vsx_tile_tool *
vsx_tile_tool_new(struct vsx_gl *gl,
                  struct vsx_shell_interface *shell,
                  struct vsx_image_loader *image_loader,
                  struct vsx_map_buffer *map_buffer,
                  struct vsx_quad_tool *quad_tool)
{
        struct vsx_tile_tool *tool = vsx_calloc(sizeof *tool);

        vsx_signal_init(&tool->ready_signal);

        tool->gl = gl;
        tool->shell = shell;
        tool->image_loader = image_loader;
        tool->map_buffer = map_buffer;
        tool->quad_tool = quad_tool;

        start_texture_load(tool, 0);

        return tool;
}

struct vsx_tile_tool_buffer *
vsx_tile_tool_create_buffer(struct vsx_tile_tool *tool,
                            int tile_size)
{
        struct vsx_tile_tool_buffer *buf = vsx_calloc(sizeof *buf);

        buf->tool = tool;
        buf->tile_size = tile_size;

        vsx_buffer_init(&buf->draw_calls);

        return buf;
}

static void
free_buffer(struct vsx_tile_tool_buffer *buf)
{
        struct vsx_gl *gl = buf->tool->gl;

        if (buf->vao) {
                vsx_array_object_free(buf->vao, gl);
                buf->vao = 0;
        }
        if (buf->vbo) {
                gl->glDeleteBuffers(1, &buf->vbo);
                buf->vbo = 0;
        }
        if (buf->quad_buffer) {
                vsx_quad_tool_unref_buffer(buf->quad_buffer, gl);
                buf->quad_buffer = NULL;
        }
}

static void
ensure_buffer_size(struct vsx_tile_tool_buffer *buf,
                   int max_tiles)
{
        if (buf->max_tiles >= max_tiles)
                return;

        free_buffer(buf);

        int n_vertices = max_tiles * 4;

        struct vsx_gl *gl = buf->tool->gl;

        gl->glGenBuffers(1, &buf->vbo);
        gl->glBindBuffer(GL_ARRAY_BUFFER, buf->vbo);
        gl->glBufferData(GL_ARRAY_BUFFER,
                         n_vertices * sizeof (struct vertex),
                         NULL, /* data */
                         GL_DYNAMIC_DRAW);

        buf->vao = vsx_array_object_new(gl);

        vsx_array_object_set_attribute(buf->vao,
                                       gl,
                                       VSX_SHADER_DATA_ATTRIB_POSITION,
                                       2, /* size */
                                       GL_FLOAT,
                                       false, /* normalized */
                                       sizeof (struct vertex),
                                       buf->vbo,
                                       offsetof(struct vertex, x));
        vsx_array_object_set_attribute(buf->vao,
                                       gl,
                                       VSX_SHADER_DATA_ATTRIB_TEX_COORD,
                                       2, /* size */
                                       GL_UNSIGNED_SHORT,
                                       true, /* normalized */
                                       sizeof (struct vertex),
                                       buf->vbo,
                                       offsetof(struct vertex, s));

        buf->quad_buffer =
                vsx_quad_tool_get_buffer(buf->tool->quad_tool,
                                         buf->vao,
                                         max_tiles);

        buf->max_tiles = max_tiles;
}

void
vsx_tile_tool_begin_update(struct vsx_tile_tool_buffer *buf,
                           int max_tiles)
{
        assert(buf->vertices == NULL);

        ensure_buffer_size(buf, max_tiles);

        struct vsx_gl *gl = buf->tool->gl;

        gl->glBindBuffer(GL_ARRAY_BUFFER, buf->vbo);

        buf->vertices =
                vsx_map_buffer_map(buf->tool->map_buffer,
                                   GL_ARRAY_BUFFER,
                                   buf->max_tiles *
                                   4 * sizeof (struct vertex),
                                   true, /* flush_explicit */
                                   GL_DYNAMIC_DRAW);

        buf->v = buf->vertices;
        buf->draw_calls.length = 0;
}

static void
add_to_draw_call_for_texture(struct vsx_tile_tool_buffer *buf,
                             int texture)
{
        if (buf->draw_calls.length > 0) {
                struct draw_call *last_draw_call =
                        (struct draw_call *)
                        (buf->draw_calls.data +
                         buf->draw_calls.length -
                         sizeof (struct draw_call));

                if (last_draw_call->texture == texture) {
                        last_draw_call->n_tiles++;
                        return;
                }
        }

        struct draw_call draw_call = {
                .n_tiles = 1,
                .texture = texture,
        };

        vsx_buffer_append(&buf->draw_calls, &draw_call, sizeof draw_call);
}

void
vsx_tile_tool_add_tile(struct vsx_tile_tool_buffer *buf,
                       int tile_x, int tile_y,
                       const struct vsx_tile_texture_letter *letter_data)
{
        struct vertex *v = buf->v;

        assert(buf->vertices);

        add_to_draw_call_for_texture(buf, letter_data->texture);

        v->x = tile_x;
        v->y = tile_y;
        v->s = letter_data->s1;
        v->t = letter_data->t1;
        v++;
        v->x = tile_x;
        v->y = tile_y + buf->tile_size;
        v->s = letter_data->s1;
        v->t = letter_data->t2;
        v++;
        v->x = tile_x + buf->tile_size;
        v->y = tile_y;
        v->s = letter_data->s2;
        v->t = letter_data->t1;
        v++;
        v->x = tile_x + buf->tile_size;
        v->y = tile_y + buf->tile_size;
        v->s = letter_data->s2;
        v->t = letter_data->t2;
        v++;

        buf->v = v;
}

void
vsx_tile_tool_end_update(struct vsx_tile_tool_buffer *buf)
{
        assert(buf->vertices);

        size_t n_vertices = buf->v - buf->vertices;

        assert(n_vertices <= buf->max_tiles * 4);

        vsx_map_buffer_flush(buf->tool->map_buffer,
                             0,
                             n_vertices * sizeof (struct vertex));

        vsx_map_buffer_unmap(buf->tool->map_buffer);

        buf->vertices = NULL;
}

void
vsx_tile_tool_paint(struct vsx_tile_tool_buffer *buf,
                    const struct vsx_shader_data *shader_data,
                    const GLfloat *matrix,
                    const GLfloat *translation)
{
        assert(vsx_tile_tool_is_ready(buf->tool));

        struct vsx_gl *gl = buf->tool->gl;

        const struct vsx_shader_data_program_data *program =
                shader_data->programs + VSX_SHADER_DATA_PROGRAM_TEXTURE;

        gl->glUseProgram(program->program);
        vsx_array_object_bind(buf->vao, gl);

        gl->glUniformMatrix2fv(program->matrix_uniform,
                               1, /* count */
                               GL_FALSE, /* transpose */
                               matrix);
        gl->glUniform2f(program->translation_uniform,
                        translation[0],
                        translation[1]);

        const struct draw_call *draw_calls =
                (const struct draw_call *) buf->draw_calls.data;
        size_t n_draw_calls = buf->draw_calls.length / sizeof *draw_calls;
        int tile_num = 0;
        int index_size = buf->quad_buffer->type == GL_UNSIGNED_BYTE ? 1 : 2;

        for (unsigned i = 0; i < n_draw_calls; i++) {
                int texture = draw_calls[i].texture;

                gl->glBindTexture(GL_TEXTURE_2D, buf->tool->textures[texture]);

                vsx_gl_draw_range_elements(gl,
                                           GL_TRIANGLES,
                                           tile_num * 4,
                                           (tile_num + draw_calls[i].n_tiles)
                                           * 4
                                           - 1,
                                           draw_calls[i].n_tiles * 6,
                                           buf->quad_buffer->type,
                                           (GLvoid *) (intptr_t)
                                           (tile_num * 6 * index_size));

                tile_num += draw_calls[i].n_tiles;
        }
}

void
vsx_tile_tool_free_buffer(struct vsx_tile_tool_buffer *buf)
{
        assert(buf->vertices == NULL);
        vsx_buffer_destroy(&buf->draw_calls);
        free_buffer(buf);
        vsx_free(buf);
}

struct vsx_signal *
vsx_tile_tool_get_ready_signal(struct vsx_tile_tool *tool)
{
        return &tool->ready_signal;
}

bool
vsx_tile_tool_is_ready(struct vsx_tile_tool *tool)
{
        for (int i = 0; i < VSX_TILE_TEXTURE_N_TEXTURES; i++) {
                if (tool->textures[i] == 0)
                        return false;
        }

        return true;
}

void
vsx_tile_tool_free(struct vsx_tile_tool *tool)
{
        struct vsx_gl *gl = tool->gl;

        if (tool->image_token)
                vsx_image_loader_cancel(tool->image_token);

        for (int i = 0; i < VSX_TILE_TEXTURE_N_TEXTURES; i++) {
                if (tool->textures[i])
                        gl->glDeleteTextures(1, &tool->textures[i]);
        }

        vsx_free(tool);
}
