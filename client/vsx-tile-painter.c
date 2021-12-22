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
#include "vsx-board.h"

struct vsx_tile_painter {
        struct vsx_game_state *game_state;
        struct vsx_painter_toolbox *toolbox;

        GLuint program;
        GLint matrix_uniform;
        GLint translation_uniform;

        struct vsx_array_object *vao;
        GLuint vbo;
        GLuint element_buffer;

        GLuint tex;
        struct vsx_image_loader_token *image_token;

        struct vsx_signal redraw_needed_signal;

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

        vsx_signal_emit(&painter->redraw_needed_signal, NULL);
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

        painter->matrix_uniform =
                vsx_gl.glGetUniformLocation(painter->program,
                                            "transform_matrix");
        painter->translation_uniform =
                vsx_gl.glGetUniformLocation(painter->program,
                                            "translation");
}

static void *
create_cb(struct vsx_game_state *game_state,
          struct vsx_painter_toolbox *toolbox)
{
        struct vsx_tile_painter *painter = vsx_calloc(sizeof *painter);

        painter->game_state = game_state;
        painter->toolbox = toolbox;

        vsx_signal_init(&painter->redraw_needed_signal);

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
        struct vertex *vertices;
        int tile_num;
};

static void
tile_cb(const struct vsx_game_state_tile *tile,
        void *user_data)
{
        struct tile_closure *closure = user_data;

        const struct vsx_tile_texture_letter *letter_data =
                find_letter(tile->letter);

        if (letter_data == NULL)
                return;

        struct vertex *v = closure->vertices + closure->tile_num * 4;

        v->x = tile->x;
        v->y = tile->y;
        v->s = letter_data->s1;
        v->t = letter_data->t1;
        v++;
        v->x = tile->x;
        v->y = tile->y + TILE_SIZE;
        v->s = letter_data->s1;
        v->t = letter_data->t2;
        v++;
        v->x = tile->x + TILE_SIZE;
        v->y = tile->y;
        v->s = letter_data->s2;
        v->t = letter_data->t1;
        v++;
        v->x = tile->x + TILE_SIZE;
        v->y = tile->y + TILE_SIZE;
        v->s = letter_data->s2;
        v->t = letter_data->t2;
        v++;

        closure->tile_num++;
}

static void
paint_cb(void *painter_data)
{
        struct vsx_tile_painter *painter = painter_data;

        if (painter->tex == 0)
                return;

        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        vsx_paint_state_ensure_layout(paint_state);

        int n_tiles = vsx_game_state_get_n_tiles(painter->game_state);

        if (n_tiles <= 0)
                return;

        ensure_buffer_size(painter, n_tiles);

        struct tile_closure closure = {
                .tile_num = 0,
        };

        vsx_gl.glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);

        closure.vertices = vsx_map_buffer_map(GL_ARRAY_BUFFER,
                                              painter->buffer_n_tiles *
                                              4 * sizeof (struct vertex),
                                              true, /* flush_explicit */
                                              GL_DYNAMIC_DRAW);

        vsx_game_state_foreach_tile(painter->game_state, tile_cb, &closure);

        vsx_map_buffer_flush(0, closure.tile_num * 4 * sizeof (struct vertex));

        vsx_map_buffer_unmap();

        /* This shouldn’t happen unless for some reason all of the
         * tiles that the server sent had letters that we don’t
         * recognise.
         */
        if (closure.tile_num <= 0)
                return;

        vsx_gl.glUseProgram(painter->program);
        vsx_array_object_bind(painter->vao);

        vsx_gl.glUniformMatrix2fv(painter->matrix_uniform,
                                  1, /* count */
                                  GL_FALSE, /* transpose */
                                  paint_state->board_matrix);
        vsx_gl.glUniform2f(painter->translation_uniform,
                           paint_state->board_translation[0],
                           paint_state->board_translation[1]);

        vsx_gl.glBindTexture(GL_TEXTURE_2D, painter->tex);

        vsx_gl.glEnable(GL_SCISSOR_TEST);
        vsx_gl.glScissor(paint_state->board_scissor_x,
                         paint_state->board_scissor_y,
                         paint_state->board_scissor_width,
                         paint_state->board_scissor_height);

        vsx_gl_draw_range_elements(GL_TRIANGLES,
                                   0, closure.tile_num * 4 - 1,
                                   closure.tile_num * 6,
                                   GL_UNSIGNED_SHORT,
                                   NULL /* indices */);

        vsx_gl.glDisable(GL_SCISSOR_TEST);
}

static struct vsx_signal *
get_redraw_needed_signal_cb(void *painter_data)
{
        struct vsx_tile_painter *painter = painter_data;

        return &painter->redraw_needed_signal;
}

static void
free_cb(void *painter_data)
{
        struct vsx_tile_painter *painter = painter_data;

        free_buffer(painter);

        if (painter->image_token)
                vsx_image_loader_cancel(painter->image_token);
        if (painter->tex)
                vsx_gl.glDeleteTextures(1, &painter->tex);

        vsx_free(painter);
}

const struct vsx_painter
vsx_tile_painter = {
        .create_cb = create_cb,
        .paint_cb = paint_cb,
        .get_redraw_needed_signal_cb = get_redraw_needed_signal_cb,
        .free_cb = free_cb,
};
