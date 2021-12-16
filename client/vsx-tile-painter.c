/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2021  Neil Roberts
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

#include "vsx-tile-painter.h"

#include <stdbool.h>
#include <math.h>
#include <stdio.h>

#include "vsx-map-buffer.h"
#include "vsx-quad-buffer.h"
#include "vsx-tile-texture.h"
#include "vsx-mipmap.h"
#include "vsx-gl.h"

struct vsx_tile_painter {
        GLuint program;

        struct vsx_array_object *vao;
        GLuint vbo;
        GLuint element_buffer;

        GLuint tex;
        struct vsx_image_loader_token *image_token;

        int buffer_n_tiles;
};

struct vertex {
        float x, y;
        uint16_t s, t;
};

#define TILE_SIZE 20

static void
texture_load_cb(const struct vsx_image *image,
                struct vsx_error *error,
                void *data)
{
        struct vsx_tile_painter *painter = data;

        painter->image_token = NULL;

        if (error) {
                fprintf(stderr,
                        "error loading tiles image: %s\n",
                        error->message);
                return;
        }

        vsx_gl.glGenTextures(1, &painter->tex);

        vsx_gl.glBindTexture(GL_TEXTURE_2D, painter->tex);
        vsx_gl.glTexParameteri(GL_TEXTURE_2D,
                               GL_TEXTURE_WRAP_S,
                               GL_CLAMP_TO_EDGE);
        vsx_gl.glTexParameteri(GL_TEXTURE_2D,
                               GL_TEXTURE_WRAP_T,
                               GL_CLAMP_TO_EDGE);
        vsx_gl.glTexParameteri(GL_TEXTURE_2D,
                               GL_TEXTURE_MIN_FILTER,
                               GL_LINEAR_MIPMAP_NEAREST);
        vsx_gl.glTexParameteri(GL_TEXTURE_2D,
                               GL_TEXTURE_MAG_FILTER,
                               GL_LINEAR);

        vsx_mipmap_load_image(image, painter->tex);
}

static void
init_program(struct vsx_tile_painter *painter,
             struct vsx_shader_data *shader_data)
{
        painter->program =
                shader_data->programs[VSX_SHADER_DATA_PROGRAM_TEXTURE];

        GLuint tex_uniform =
                vsx_gl.glGetUniformLocation(painter->program, "tex");
        vsx_gl.glUseProgram(painter->program);
        vsx_gl.glUniform1i(tex_uniform, 0);
}

struct vsx_tile_painter *
vsx_tile_painter_new(struct vsx_painter_toolbox *toolbox)
{
        struct vsx_tile_painter *painter = vsx_calloc(sizeof *painter);

        init_program(painter, &toolbox->shader_data);

        painter->image_token = vsx_image_loader_load(toolbox->image_loader,
                                                     "tiles.mpng",
                                                     texture_load_cb,
                                                     painter);

        return painter;
}

static void
free_buffer(struct vsx_tile_painter *painter)
{
        if (painter->vao) {
                vsx_array_object_free(painter->vao);
                painter->vao = 0;
        }
        if (painter->vbo) {
                vsx_gl.glDeleteBuffers(1, &painter->vbo);
                painter->vbo = 0;
        }
        if (painter->element_buffer) {
                vsx_gl.glDeleteBuffers(1, &painter->element_buffer);
                painter->element_buffer = 0;
        }
}

static void
ensure_buffer_size(struct vsx_tile_painter *painter,
                   int n_tiles)
{
        if (painter->buffer_n_tiles >= n_tiles)
                return;

        free_buffer(painter);

        int n_vertices = n_tiles * 4;

        vsx_gl.glGenBuffers(1, &painter->vbo);
        vsx_gl.glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);
        vsx_gl.glBufferData(GL_ARRAY_BUFFER,
                            n_vertices * sizeof (struct vertex),
                            NULL, /* data */
                            GL_DYNAMIC_DRAW);

        painter->vao = vsx_array_object_new();

        vsx_array_object_set_attribute(painter->vao,
                                       VSX_SHADER_DATA_ATTRIB_POSITION,
                                       2, /* size */
                                       GL_FLOAT,
                                       false, /* normalized */
                                       sizeof (struct vertex),
                                       0, /* divisor */
                                       painter->vbo,
                                       offsetof(struct vertex, x));
        vsx_array_object_set_attribute(painter->vao,
                                       VSX_SHADER_DATA_ATTRIB_TEX_COORD,
                                       2, /* size */
                                       GL_UNSIGNED_SHORT,
                                       true, /* normalized */
                                       sizeof (struct vertex),
                                       0, /* divisor */
                                       painter->vbo,
                                       offsetof(struct vertex, s));

        painter->element_buffer =
                vsx_quad_buffer_generate(painter->vao, n_tiles);

        painter->buffer_n_tiles = n_tiles;
}

static const struct vsx_tile_texture_letter *
find_letter(uint32_t letter)
{
        int min = 0, max = VSX_TILE_TEXTURE_N_LETTERS;

        while (min < max) {
                int mid = (min + max) / 2;
                uint32_t mid_letter = vsx_tile_texture_letters[mid].letter;

                if (mid_letter > letter)
                        max = mid;
                else if (mid_letter == letter)
                        return vsx_tile_texture_letters + mid;
                else
                        min = mid + 1;
        }

        return NULL;
}

struct tile_closure {
        float x_scale, y_scale;
        struct vertex *vertices;
        int tile_num;
};

static void
tile_cb(int x, int y,
        uint32_t letter,
        void *user_data)
{
        struct tile_closure *closure = user_data;

        const struct vsx_tile_texture_letter *letter_data =
                find_letter(letter);

        if (letter_data == NULL)
                return;

        struct vertex *v = closure->vertices + closure->tile_num * 4;

        float left = x * closure->x_scale - 1.0f;
        float right = left + TILE_SIZE * closure->x_scale;
        float top = 1.0f - y * closure->y_scale;
        float bottom = top - TILE_SIZE * closure->y_scale;

        v->x = left;
        v->y = top;
        v->s = letter_data->s1;
        v->t = letter_data->t1;
        v++;
        v->x = left;
        v->y = bottom;
        v->s = letter_data->s1;
        v->t = letter_data->t2;
        v++;
        v->x = right;
        v->y = top;
        v->s = letter_data->s2;
        v->t = letter_data->t1;
        v++;
        v->x = right;
        v->y = bottom;
        v->s = letter_data->s2;
        v->t = letter_data->t2;
        v++;

        closure->tile_num++;
}

void
vsx_tile_painter_paint(struct vsx_tile_painter *painter,
                       struct vsx_game_state *game_state,
                       int fb_width,
                       int fb_height)
{
        if (painter->tex == 0)
                return;

        int n_tiles = vsx_game_state_get_n_tiles(game_state);

        if (n_tiles <= 0)
                return;

        ensure_buffer_size(painter, n_tiles);

        struct tile_closure closure = {
                .x_scale = 2.0f / fb_width,
                .y_scale = 2.0f / fb_height,
                .tile_num = 0,
        };

        vsx_gl.glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);

        closure.vertices = vsx_map_buffer_map(GL_ARRAY_BUFFER,
                                              painter->buffer_n_tiles *
                                              4 * sizeof (struct vertex),
                                              true, /* flush_explicit */
                                              GL_DYNAMIC_DRAW);

        vsx_game_state_foreach_tile(game_state, tile_cb, &closure);

        vsx_map_buffer_flush(0, closure.tile_num * 4 * sizeof (struct vertex));

        vsx_map_buffer_unmap();

        vsx_gl.glUseProgram(painter->program);
        vsx_array_object_bind(painter->vao);

        vsx_gl.glBindTexture(GL_TEXTURE_2D, painter->tex);

        vsx_gl_draw_range_elements(GL_TRIANGLES,
                                   0, closure.tile_num * 4 - 1,
                                   closure.tile_num * 6,
                                   GL_UNSIGNED_SHORT,
                                   NULL /* indices */);
}

void
vsx_tile_painter_free(struct vsx_tile_painter *painter)
{
        free_buffer(painter);

        if (painter->image_token)
                vsx_image_loader_cancel(painter->image_token);
        if (painter->tex)
                vsx_gl.glDeleteTextures(1, &painter->tex);

        vsx_free(painter);
}
