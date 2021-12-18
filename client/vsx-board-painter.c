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

#include "vsx-board-painter.h"

#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <assert.h>

#include "vsx-map-buffer.h"
#include "vsx-mipmap.h"
#include "vsx-gl.h"
#include "vsx-array-object.h"
#include "vsx-quad-buffer.h"
#include "vsx-board.h"

struct vsx_board_painter {
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

#define PLAYER_SPACE_SIDE_HEIGHT 170
#define PLAYER_SPACE_SIDE_WIDTH 90
#define PLAYER_SPACE_MIDDLE_HEIGHT PLAYER_SPACE_SIDE_WIDTH
#define PLAYER_SPACE_MIDDLE_WIDTH PLAYER_SPACE_SIDE_HEIGHT
#define PLAYER_SPACE_CORNER_SIZE 40
#define PLAYER_SPACE_MIDDLE_X (VSX_BOARD_WIDTH / 2 -            \
                               PLAYER_SPACE_MIDDLE_WIDTH / 2)

struct board_quad {
        int16_t x1, y1;
        int16_t x2, y2;
        uint8_t s1, t1;
        uint8_t s2, t2;
};

static const struct board_quad
board_quads[] = {
        /* Left column */
        {
                .x1 = 0, .y1 = 0,
                .x2 = PLAYER_SPACE_SIDE_WIDTH,
                .y2 = PLAYER_SPACE_SIDE_HEIGHT - PLAYER_SPACE_CORNER_SIZE,
        },
        {
                .x1 = 0,
                .y1 = PLAYER_SPACE_SIDE_HEIGHT - PLAYER_SPACE_CORNER_SIZE,
                .x2 = PLAYER_SPACE_SIDE_WIDTH - PLAYER_SPACE_CORNER_SIZE,
                .y2 = PLAYER_SPACE_SIDE_HEIGHT,
        },
        {
                .x1 = PLAYER_SPACE_SIDE_WIDTH - PLAYER_SPACE_CORNER_SIZE,
                .y1 = PLAYER_SPACE_SIDE_HEIGHT - PLAYER_SPACE_CORNER_SIZE,
                .x2 = PLAYER_SPACE_SIDE_WIDTH,
                .y2 = PLAYER_SPACE_SIDE_HEIGHT,
                .s1 = 0, .t1 = 255,
                .s2 = 255, .t2 = 0,
        },
        {
                .x1 = 0,
                .y1 = PLAYER_SPACE_SIDE_HEIGHT,
                .x2 = PLAYER_SPACE_SIDE_WIDTH,
                .y2 = VSX_BOARD_HEIGHT - PLAYER_SPACE_SIDE_HEIGHT,
                .s1 = 255,
                .s2 = 255,
        },
        {
                .x1 = 0,
                .y1 = VSX_BOARD_HEIGHT - PLAYER_SPACE_SIDE_HEIGHT,
                .x2 = PLAYER_SPACE_SIDE_WIDTH - PLAYER_SPACE_CORNER_SIZE,
                .y2 = (VSX_BOARD_HEIGHT -
                       PLAYER_SPACE_SIDE_HEIGHT +
                       PLAYER_SPACE_CORNER_SIZE),
        },
        {
                .x1 = PLAYER_SPACE_SIDE_WIDTH - PLAYER_SPACE_CORNER_SIZE,
                .y1 = VSX_BOARD_HEIGHT - PLAYER_SPACE_SIDE_HEIGHT,
                .x2 = PLAYER_SPACE_SIDE_WIDTH,
                .y2 = (VSX_BOARD_HEIGHT -
                       PLAYER_SPACE_SIDE_HEIGHT +
                       PLAYER_SPACE_CORNER_SIZE),
                .s1 = 0, .t1 = 0,
                .s2 = 255, .t2 = 255,
        },
        {
                .x1 = 0,
                .y1 = (VSX_BOARD_HEIGHT -
                       PLAYER_SPACE_SIDE_HEIGHT +
                       PLAYER_SPACE_CORNER_SIZE),
                .x2 = PLAYER_SPACE_SIDE_WIDTH,
                .y2 = VSX_BOARD_HEIGHT,
        },

        /* Left gap */
        {
                .x1 = PLAYER_SPACE_SIDE_WIDTH,
                .y1 = 0,
                .x2 = PLAYER_SPACE_MIDDLE_X,
                .y2 = VSX_BOARD_HEIGHT,
                .s1 = 255,
                .s2 = 255,
        },

        /* Middle column */
        {
                .x1 = PLAYER_SPACE_MIDDLE_X,
                .y1 = 0,
                .x2 = PLAYER_SPACE_MIDDLE_X + PLAYER_SPACE_MIDDLE_WIDTH,
                .y2 = PLAYER_SPACE_MIDDLE_HEIGHT - PLAYER_SPACE_CORNER_SIZE,
        },
        {
                .x1 = PLAYER_SPACE_MIDDLE_X + PLAYER_SPACE_CORNER_SIZE,
                .y1 = PLAYER_SPACE_MIDDLE_HEIGHT - PLAYER_SPACE_CORNER_SIZE,
                .x2 = (PLAYER_SPACE_MIDDLE_X +
                       PLAYER_SPACE_MIDDLE_WIDTH -
                       PLAYER_SPACE_CORNER_SIZE),
                .y2 = PLAYER_SPACE_MIDDLE_HEIGHT,
        },
        {
                .x1 = PLAYER_SPACE_MIDDLE_X,
                .y1 = PLAYER_SPACE_MIDDLE_HEIGHT - PLAYER_SPACE_CORNER_SIZE,
                .x2 = PLAYER_SPACE_MIDDLE_X + PLAYER_SPACE_CORNER_SIZE,
                .y2 = PLAYER_SPACE_MIDDLE_HEIGHT,
                .s1 = 255, .t1 = 255,
                .s2 = 0, .t2 = 0,
        },
        {
                .x1 = (PLAYER_SPACE_MIDDLE_X +
                       PLAYER_SPACE_MIDDLE_WIDTH -
                       PLAYER_SPACE_CORNER_SIZE),
                .y1 = PLAYER_SPACE_MIDDLE_HEIGHT - PLAYER_SPACE_CORNER_SIZE,
                .x2 = PLAYER_SPACE_MIDDLE_X + PLAYER_SPACE_MIDDLE_WIDTH,
                .y2 = PLAYER_SPACE_MIDDLE_HEIGHT,
                .s1 = 0, .t1 = 255,
                .s2 = 255, .t2 = 0,
        },
        {
                .x1 = PLAYER_SPACE_MIDDLE_X,
                .y1 = PLAYER_SPACE_MIDDLE_HEIGHT,
                .x2 = PLAYER_SPACE_MIDDLE_X + PLAYER_SPACE_MIDDLE_WIDTH,
                .y2 = VSX_BOARD_HEIGHT - PLAYER_SPACE_MIDDLE_HEIGHT,
                .s1 = 255, .s2 = 255,
        },
        {
                .x1 = PLAYER_SPACE_MIDDLE_X,
                .y1 = VSX_BOARD_HEIGHT - PLAYER_SPACE_MIDDLE_HEIGHT,
                .x2 = PLAYER_SPACE_MIDDLE_X + PLAYER_SPACE_CORNER_SIZE,
                .y2 = (VSX_BOARD_HEIGHT -
                       PLAYER_SPACE_MIDDLE_HEIGHT +
                       PLAYER_SPACE_CORNER_SIZE),
                .s1 = 255, .t1 = 0,
                .s2 = 0, .t2 = 255,
        },
        {
                .x1 = PLAYER_SPACE_MIDDLE_X + PLAYER_SPACE_CORNER_SIZE,
                .y1 = VSX_BOARD_HEIGHT - PLAYER_SPACE_MIDDLE_HEIGHT,
                .x2 = (PLAYER_SPACE_MIDDLE_X +
                       PLAYER_SPACE_MIDDLE_WIDTH -
                       PLAYER_SPACE_CORNER_SIZE),
                .y2 = (VSX_BOARD_HEIGHT -
                       PLAYER_SPACE_MIDDLE_HEIGHT +
                       PLAYER_SPACE_CORNER_SIZE),
        },
        {
                .x1 = (PLAYER_SPACE_MIDDLE_X +
                       PLAYER_SPACE_MIDDLE_WIDTH -
                       PLAYER_SPACE_CORNER_SIZE),
                .y1 = VSX_BOARD_HEIGHT - PLAYER_SPACE_MIDDLE_HEIGHT,
                .x2 = PLAYER_SPACE_MIDDLE_X + PLAYER_SPACE_MIDDLE_WIDTH,
                .y2 = (VSX_BOARD_HEIGHT -
                       PLAYER_SPACE_MIDDLE_HEIGHT +
                       PLAYER_SPACE_CORNER_SIZE),
                .s1 = 0, .t1 = 0,
                .s2 = 255, .t2 = 255,
        },
        {
                .x1 = PLAYER_SPACE_MIDDLE_X,
                .y1 = (VSX_BOARD_HEIGHT -
                       PLAYER_SPACE_MIDDLE_HEIGHT +
                       PLAYER_SPACE_CORNER_SIZE),
                .x2 = PLAYER_SPACE_MIDDLE_X + PLAYER_SPACE_MIDDLE_WIDTH,
                .y2 = VSX_BOARD_HEIGHT,
        },

        /* Right gap */
        {
                .x1 = PLAYER_SPACE_MIDDLE_X + PLAYER_SPACE_MIDDLE_WIDTH,
                .y1 = 0,
                .x2 = VSX_BOARD_WIDTH - PLAYER_SPACE_SIDE_WIDTH,
                .y2 = VSX_BOARD_HEIGHT,
                .s1 = 255, .s2 = 255,
        },

        /* Right column */
        {
                .x1 = VSX_BOARD_WIDTH - PLAYER_SPACE_SIDE_WIDTH,
                .y1 = 0,
                .x2 = VSX_BOARD_WIDTH,
                .y2 = PLAYER_SPACE_SIDE_HEIGHT - PLAYER_SPACE_CORNER_SIZE,
        },
        {
                .x1 = VSX_BOARD_WIDTH - PLAYER_SPACE_SIDE_WIDTH,
                .y1 = PLAYER_SPACE_SIDE_HEIGHT - PLAYER_SPACE_CORNER_SIZE,
                .x2 = (VSX_BOARD_WIDTH -
                       PLAYER_SPACE_SIDE_WIDTH +
                       PLAYER_SPACE_CORNER_SIZE),
                .y2 = PLAYER_SPACE_SIDE_HEIGHT,
                .s1 = 255, .t1 = 255,
                .s2 = 0, .t2 = 0,
        },
        {
                .x1 = (VSX_BOARD_WIDTH -
                       PLAYER_SPACE_SIDE_WIDTH +
                       PLAYER_SPACE_CORNER_SIZE),
                .y1 = PLAYER_SPACE_SIDE_HEIGHT - PLAYER_SPACE_CORNER_SIZE,
                .x2 = VSX_BOARD_WIDTH,
                .y2 = PLAYER_SPACE_SIDE_HEIGHT,
        },
        {
                .x1 = VSX_BOARD_WIDTH - PLAYER_SPACE_SIDE_WIDTH,
                .y1 = PLAYER_SPACE_SIDE_HEIGHT,
                .x2 = VSX_BOARD_WIDTH,
                .y2 = VSX_BOARD_HEIGHT - PLAYER_SPACE_SIDE_HEIGHT,
                .s1 = 255,
                .s2 = 255,
        },
        {
                .x1 = VSX_BOARD_WIDTH - PLAYER_SPACE_SIDE_WIDTH,
                .y1 = VSX_BOARD_HEIGHT - PLAYER_SPACE_SIDE_HEIGHT,
                .x2 = (VSX_BOARD_WIDTH -
                       PLAYER_SPACE_SIDE_WIDTH +
                       PLAYER_SPACE_CORNER_SIZE),
                .y2 = (VSX_BOARD_HEIGHT -
                       PLAYER_SPACE_SIDE_HEIGHT +
                       PLAYER_SPACE_CORNER_SIZE),
                .s1 = 255, .t1 = 0,
                .s2 = 0, .t2 = 255,
        },
        {
                .x1 = (VSX_BOARD_WIDTH -
                       PLAYER_SPACE_SIDE_WIDTH +
                       PLAYER_SPACE_CORNER_SIZE),
                .y1 = VSX_BOARD_HEIGHT - PLAYER_SPACE_SIDE_HEIGHT,
                .x2 = VSX_BOARD_WIDTH,
                .y2 = (VSX_BOARD_HEIGHT -
                       PLAYER_SPACE_SIDE_HEIGHT +
                       PLAYER_SPACE_CORNER_SIZE),
        },
        {
                .x1 = VSX_BOARD_WIDTH - PLAYER_SPACE_SIDE_WIDTH,
                .y1 = (VSX_BOARD_HEIGHT -
                       PLAYER_SPACE_SIDE_HEIGHT +
                       PLAYER_SPACE_CORNER_SIZE),
                .x2 = VSX_BOARD_WIDTH,
                .y2 = VSX_BOARD_HEIGHT,
        },

};

#define N_QUADS VSX_N_ELEMENTS(board_quads)
#define N_VERTICES (N_QUADS * 4)

static void
texture_load_cb(const struct vsx_image *image,
                struct vsx_error *error,
                void *data)
{
        struct vsx_board_painter *painter = data;

        painter->image_token = NULL;

        if (error) {
                fprintf(stderr,
                        "error loading board image: %s\n",
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
generate_vertices(struct vertex *vertices)
{
        struct vertex *v = vertices;

        for (int i = 0; i < N_QUADS; i++) {
                const struct board_quad *quad = board_quads + i;

                v->x = quad->x1;
                v->y = quad->y1;
                v->s = quad->s1;
                v->t = quad->t1;
                v++;
                v->x = quad->x1;
                v->y = quad->y2;
                v->s = quad->s1;
                v->t = quad->t2;
                v++;
                v->x = quad->x2;
                v->y = quad->y1;
                v->s = quad->s2;
                v->t = quad->t1;
                v++;
                v->x = quad->x2;
                v->y = quad->y2;
                v->s = quad->s2;
                v->t = quad->t2;
                v++;
        }

        assert(v - vertices == N_VERTICES);
}

static void
create_buffer(struct vsx_board_painter *painter)
{
        vsx_gl.glGenBuffers(1, &painter->vbo);
        vsx_gl.glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);
        vsx_gl.glBufferData(GL_ARRAY_BUFFER,
                            N_VERTICES * sizeof (struct vertex),
                            NULL, /* data */
                            GL_STATIC_DRAW);

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

        struct vertex *vertices =
                vsx_map_buffer_map(GL_ARRAY_BUFFER,
                                   N_VERTICES * sizeof (struct vertex),
                                   false, /* flush explicit */
                                   GL_STATIC_DRAW);
        generate_vertices(vertices);
        vsx_map_buffer_unmap();

        painter->element_buffer =
                vsx_quad_buffer_generate(painter->vao, N_QUADS);
}

static void
init_program(struct vsx_board_painter *painter,
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
        struct vsx_board_painter *painter = vsx_calloc(sizeof *painter);

        vsx_signal_init(&painter->redraw_needed_signal);

        init_program(painter, &toolbox->shader_data);

        create_buffer(painter);

        painter->image_token = vsx_image_loader_load(toolbox->image_loader,
                                                     "board.mpng",
                                                     texture_load_cb,
                                                     painter);

        return painter;
}

static void
paint_cb(void *painter_data,
         struct vsx_game_state *game_state,
         const struct vsx_paint_state *paint_state)
{
        struct vsx_board_painter *painter = painter_data;

        if (painter->tex == 0)
                return;

        vsx_gl.glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);

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

        vsx_gl_draw_range_elements(GL_TRIANGLES,
                                   0, N_VERTICES - 1,
                                   N_QUADS * 6,
                                   GL_UNSIGNED_SHORT,
                                   NULL /* indices */);
}

static struct vsx_signal *
get_redraw_needed_signal_cb(void *painter_data)
{
        struct vsx_board_painter *painter = painter_data;

        return &painter->redraw_needed_signal;
}

static void
free_cb(void *painter_data)
{
        struct vsx_board_painter *painter = painter_data;

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
vsx_board_painter = {
        .create_cb = create_cb,
        .paint_cb = paint_cb,
        .get_redraw_needed_signal_cb = get_redraw_needed_signal_cb,
        .free_cb = free_cb,
};
