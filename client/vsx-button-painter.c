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

#include "vsx-button-painter.h"

#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <assert.h>

#include "vsx-map-buffer.h"
#include "vsx-mipmap.h"
#include "vsx-gl.h"
#include "vsx-array-object.h"
#include "vsx-quad-buffer.h"

struct vsx_button_painter {
        GLuint program;
        GLint matrix_uniform;
        GLint translation_uniform;

        struct vsx_array_object *vao;
        GLuint vbo;
        GLuint element_buffer;

        GLuint tex;
        struct vsx_image_loader_token *image_token;

        struct vsx_signal redraw_needed_signal;
};

struct vertex {
        int16_t x, y;
        uint8_t s, t;
};

#define N_QUADS 4
#define N_VERTICES (N_QUADS * 4)

static void
texture_load_cb(const struct vsx_image *image,
                struct vsx_error *error,
                void *data)
{
        struct vsx_button_painter *painter = data;

        painter->image_token = NULL;

        if (error) {
                fprintf(stderr,
                        "error loading button image: %s\n",
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
create_buffer(struct vsx_button_painter *painter)
{
        vsx_gl.glGenBuffers(1, &painter->vbo);
        vsx_gl.glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);
        vsx_gl.glBufferData(GL_ARRAY_BUFFER,
                            N_VERTICES * sizeof (struct vertex),
                            NULL, /* data */
                            GL_DYNAMIC_DRAW);

        painter->vao = vsx_array_object_new();

        vsx_array_object_set_attribute(painter->vao,
                                       VSX_SHADER_DATA_ATTRIB_POSITION,
                                       2, /* size */
                                       GL_SHORT,
                                       false, /* normalized */
                                       sizeof (struct vertex),
                                       0, /* divisor */
                                       painter->vbo,
                                       offsetof(struct vertex, x));
        vsx_array_object_set_attribute(painter->vao,
                                       VSX_SHADER_DATA_ATTRIB_TEX_COORD,
                                       2, /* size */
                                       GL_UNSIGNED_BYTE,
                                       true, /* normalized */
                                       sizeof (struct vertex),
                                       0, /* divisor */
                                       painter->vbo,
                                       offsetof(struct vertex, s));

        painter->element_buffer =
                vsx_quad_buffer_generate(painter->vao, N_QUADS);
}

static void
init_program(struct vsx_button_painter *painter,
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
create_cb(struct vsx_painter_toolbox *toolbox)
{
        struct vsx_button_painter *painter = vsx_calloc(sizeof *painter);

        vsx_signal_init(&painter->redraw_needed_signal);

        init_program(painter, &toolbox->shader_data);

        create_buffer(painter);

        painter->image_token = vsx_image_loader_load(toolbox->image_loader,
                                                     "buttons.mpng",
                                                     texture_load_cb,
                                                     painter);

        return painter;
}

static void
calculate_transform(struct vsx_button_painter *painter,
                    const struct vsx_paint_state *paint_state,
                    int *area_width,
                    int *area_height)
{
        float matrix[4], translation[2];

        if (paint_state->board_rotated) {
                matrix[0] = 0.0f;
                matrix[1] = -2.0f / paint_state->height;
                matrix[2] = -2.0f / paint_state->width;
                matrix[3] = 0.0f;
                translation[0] = 1.0f;
                translation[1] = ((paint_state->board_scissor_height -
                                   paint_state->height / 2.0f) *
                                  matrix[1]);
                *area_width = (paint_state->height -
                               paint_state->board_scissor_height);
                *area_height = paint_state->width;
        } else {
                matrix[0] = 2.0f / paint_state->width;
                matrix[1] = 0.0f;
                matrix[2] = 0.0f;
                matrix[3] = -2.0f / paint_state->height;
                translation[0] = ((paint_state->board_scissor_width -
                                   paint_state->width / 2.0f) *
                                  matrix[0]);
                translation[1] = 1.0f;
                *area_width = (paint_state->width -
                               paint_state->board_scissor_width);
                *area_height = paint_state->height;
        }

        vsx_gl.glUniformMatrix2fv(painter->matrix_uniform,
                                  1, /* count */
                                  GL_FALSE, /* transpose */
                                  matrix);
        vsx_gl.glUniform2f(painter->translation_uniform,
                           translation[0],
                           translation[1]);
}

static void
store_quad(struct vertex *vertices,
           int x, int y,
           int w, int h,
           int s, int t,
           int tw, int th)
{
        struct vertex *v = vertices;

        v->x = x;
        v->y = y;
        v->s = s;
        v->t = t;
        v++;
        v->x = x;
        v->y = y + h;
        v->s = s;
        v->t = t + th;
        v++;
        v->x = x + w;
        v->y = y;
        v->s = s + tw;
        v->t = t;
        v++;
        v->x = x + w;
        v->y = y + h;
        v->s = s + tw;
        v->t = t + th;
}

static void
generate_vertices(struct vsx_button_painter *painter,
                  struct vertex *vertices,
                  int area_width, int area_height)
{
        store_quad(vertices + 0,
                   0, 0,
                   10, 10,
                   0, 0,
                   128, 128);
        store_quad(vertices + 4,
                   area_width - 10, 0,
                   10, 10,
                   128, 0,
                   127, 128);
        store_quad(vertices + 8,
                   0, area_height - 10,
                   10, 10,
                   0, 128,
                   128, 127);
        store_quad(vertices + 12,
                   area_width - 10, area_height - 10,
                   10, 10,
                   128, 128,
                   127, 127);
}

static void
paint_cb(void *painter_data,
         struct vsx_game_state *game_state,
         const struct vsx_paint_state *paint_state)
{
        struct vsx_button_painter *painter = painter_data;

        if (painter->tex == 0)
                return;

        int area_width, area_height;

        calculate_transform(painter,
                            paint_state,
                            &area_width,
                            &area_height);

        vsx_gl.glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);

        struct vertex *vertices =
                vsx_map_buffer_map(GL_ARRAY_BUFFER,
                                   N_VERTICES * sizeof (struct vertex),
                                   false, /* flush explicit */
                                   GL_DYNAMIC_DRAW);

        generate_vertices(painter, vertices, area_width, area_height);

        vsx_map_buffer_unmap();

        vsx_gl.glUseProgram(painter->program);
        vsx_array_object_bind(painter->vao);

        vsx_gl.glBindTexture(GL_TEXTURE_2D, painter->tex);

        vsx_gl_draw_range_elements(GL_TRIANGLES,
                                   0, N_VERTICES - 1,
                                   N_QUADS * 6,
                                   GL_UNSIGNED_SHORT,
                                   NULL /* indices */);
}

static struct vsx_signal *
get_redraw_needed_signal_cb(void *painter_data)
{
        struct vsx_button_painter *painter = painter_data;

        return &painter->redraw_needed_signal;
}

static void
free_cb(void *painter_data)
{
        struct vsx_button_painter *painter = painter_data;

        if (painter->vao)
                vsx_array_object_free(painter->vao);
        if (painter->vbo)
                vsx_gl.glDeleteBuffers(1, &painter->vbo);
        if (painter->element_buffer)
                vsx_gl.glDeleteBuffers(1, &painter->element_buffer);

        if (painter->image_token)
                vsx_image_loader_cancel(painter->image_token);
        if (painter->tex)
                vsx_gl.glDeleteTextures(1, &painter->tex);

        vsx_free(painter);
}

const struct vsx_painter
vsx_button_painter = {
        .create_cb = create_cb,
        .paint_cb = paint_cb,
        .get_redraw_needed_signal_cb = get_redraw_needed_signal_cb,
        .free_cb = free_cb,
};
