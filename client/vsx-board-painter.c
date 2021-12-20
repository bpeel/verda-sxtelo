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

#define N_BOX_STYLES 3

struct box_style_draw_command {
        int offset;
        int min_vertex, max_vertex;
};

struct box_draw_command {
        struct box_style_draw_command styles[N_BOX_STYLES];
};

struct vsx_board_painter {
        GLuint program;
        GLint matrix_uniform;
        GLint translation_uniform;

        struct box_draw_command box_draw_commands
        [VSX_GAME_STATE_N_VISIBLE_PLAYERS];

        struct vsx_array_object *vao;
        GLuint vbo;
        GLuint element_buffer;

        GLuint tex;
        struct vsx_image_loader_token *image_token;

        struct vsx_signal redraw_needed_signal;
};

struct vertex {
        int16_t x, y;
        uint16_t s, t;
};

#define PLAYER_SPACE_SIDE_HEIGHT 170
#define PLAYER_SPACE_SIDE_WIDTH 90
#define PLAYER_SPACE_MIDDLE_HEIGHT PLAYER_SPACE_SIDE_WIDTH
#define PLAYER_SPACE_MIDDLE_WIDTH PLAYER_SPACE_SIDE_HEIGHT
#define PLAYER_SPACE_CORNER_SIZE 20
#define PLAYER_SPACE_MIDDLE_X (VSX_BOARD_WIDTH / 2 -            \
                               PLAYER_SPACE_MIDDLE_WIDTH / 2)

struct board_quad {
        int16_t x1, y1;
        int16_t x2, y2;
        /* The texture coordinates are always either 0 or 1 to
         * represent a side of the corner image.
         */
        uint8_t s1, t1;
        uint8_t s2, t2;
};

static const struct board_quad
board_quads[] = {
        /* Gap between the players on the left */
        {
                .x1 = 0,
                .y1 = PLAYER_SPACE_SIDE_HEIGHT,
                .x2 = PLAYER_SPACE_SIDE_WIDTH,
                .y2 = VSX_BOARD_HEIGHT - PLAYER_SPACE_SIDE_HEIGHT,
                .s1 = 1,
                .s2 = 1,
        },

        /* Left gap */
        {
                .x1 = PLAYER_SPACE_SIDE_WIDTH,
                .y1 = 0,
                .x2 = PLAYER_SPACE_MIDDLE_X,
                .y2 = VSX_BOARD_HEIGHT,
                .s1 = 1,
                .s2 = 1,
        },

        /* Middle gap */
        {
                .x1 = PLAYER_SPACE_MIDDLE_X,
                .y1 = PLAYER_SPACE_MIDDLE_HEIGHT,
                .x2 = PLAYER_SPACE_MIDDLE_X + PLAYER_SPACE_MIDDLE_WIDTH,
                .y2 = VSX_BOARD_HEIGHT - PLAYER_SPACE_MIDDLE_HEIGHT,
                .s1 = 1, .s2 = 1,
        },

        /* Right gap */
        {
                .x1 = PLAYER_SPACE_MIDDLE_X + PLAYER_SPACE_MIDDLE_WIDTH,
                .y1 = 0,
                .x2 = VSX_BOARD_WIDTH - PLAYER_SPACE_SIDE_WIDTH,
                .y2 = VSX_BOARD_HEIGHT,
                .s1 = 1, .s2 = 1,
        },

        /* Gap between players on the right */
        {
                .x1 = VSX_BOARD_WIDTH - PLAYER_SPACE_SIDE_WIDTH,
                .y1 = PLAYER_SPACE_SIDE_HEIGHT,
                .x2 = VSX_BOARD_WIDTH,
                .y2 = VSX_BOARD_HEIGHT - PLAYER_SPACE_SIDE_HEIGHT,
                .s1 = 1,
                .s2 = 1,
        },
};

static const struct board_quad
player_0_quads[] = {
        /* Top middle */
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
                .s1 = 1, .t1 = 1,
                .s2 = 0, .t2 = 0,
        },
        {
                .x1 = (PLAYER_SPACE_MIDDLE_X +
                       PLAYER_SPACE_MIDDLE_WIDTH -
                       PLAYER_SPACE_CORNER_SIZE),
                .y1 = PLAYER_SPACE_MIDDLE_HEIGHT - PLAYER_SPACE_CORNER_SIZE,
                .x2 = PLAYER_SPACE_MIDDLE_X + PLAYER_SPACE_MIDDLE_WIDTH,
                .y2 = PLAYER_SPACE_MIDDLE_HEIGHT,
                .s1 = 0, .t1 = 1,
                .s2 = 1, .t2 = 0,
        },
};

static const struct board_quad
player_1_quads[] = {
        /* Bottom middle */
        {
                .x1 = PLAYER_SPACE_MIDDLE_X,
                .y1 = VSX_BOARD_HEIGHT - PLAYER_SPACE_MIDDLE_HEIGHT,
                .x2 = PLAYER_SPACE_MIDDLE_X + PLAYER_SPACE_CORNER_SIZE,
                .y2 = (VSX_BOARD_HEIGHT -
                       PLAYER_SPACE_MIDDLE_HEIGHT +
                       PLAYER_SPACE_CORNER_SIZE),
                .s1 = 1, .t1 = 0,
                .s2 = 0, .t2 = 1,
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
                .s2 = 1, .t2 = 1,
        },
        {
                .x1 = PLAYER_SPACE_MIDDLE_X,
                .y1 = (VSX_BOARD_HEIGHT -
                       PLAYER_SPACE_MIDDLE_HEIGHT +
                       PLAYER_SPACE_CORNER_SIZE),
                .x2 = PLAYER_SPACE_MIDDLE_X + PLAYER_SPACE_MIDDLE_WIDTH,
                .y2 = VSX_BOARD_HEIGHT,
        },
};

static const struct board_quad
player_2_quads[] = {
        /* Top left */
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
                .s1 = 0, .t1 = 1,
                .s2 = 1, .t2 = 0,
        },
};

static const struct board_quad
player_3_quads[] = {
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
                .s1 = 1, .t1 = 1,
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
};

static const struct board_quad
player_4_quads[] = {
        /* Bottom left */
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
                .s2 = 1, .t2 = 1,
        },
        {
                .x1 = 0,
                .y1 = (VSX_BOARD_HEIGHT -
                       PLAYER_SPACE_SIDE_HEIGHT +
                       PLAYER_SPACE_CORNER_SIZE),
                .x2 = PLAYER_SPACE_SIDE_WIDTH,
                .y2 = VSX_BOARD_HEIGHT,
        },
};

static const struct board_quad
player_5_quads[] = {
        {
                .x1 = VSX_BOARD_WIDTH - PLAYER_SPACE_SIDE_WIDTH,
                .y1 = VSX_BOARD_HEIGHT - PLAYER_SPACE_SIDE_HEIGHT,
                .x2 = (VSX_BOARD_WIDTH -
                       PLAYER_SPACE_SIDE_WIDTH +
                       PLAYER_SPACE_CORNER_SIZE),
                .y2 = (VSX_BOARD_HEIGHT -
                       PLAYER_SPACE_SIDE_HEIGHT +
                       PLAYER_SPACE_CORNER_SIZE),
                .s1 = 1, .t1 = 0,
                .s2 = 0, .t2 = 1,
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

struct player_box {
        int n_quads;
        const struct board_quad *quads;
};

static const struct player_box
player_boxes[] = {
        {
                .n_quads = VSX_N_ELEMENTS(player_0_quads),
                .quads = player_0_quads,
        },
        {
                .n_quads = VSX_N_ELEMENTS(player_1_quads),
                .quads = player_1_quads,
        },
        {
                .n_quads = VSX_N_ELEMENTS(player_2_quads),
                .quads = player_2_quads,
        },
        {
                .n_quads = VSX_N_ELEMENTS(player_3_quads),
                .quads = player_3_quads,
        },
        {
                .n_quads = VSX_N_ELEMENTS(player_4_quads),
                .quads = player_4_quads,
        },
        {
                .n_quads = VSX_N_ELEMENTS(player_5_quads),
                .quads = player_5_quads,
        },
};

_Static_assert(VSX_N_ELEMENTS(player_boxes) == VSX_GAME_STATE_N_VISIBLE_PLAYERS,
               "The number of defined player boxes doesn’t match the number "
               "of visible players");

#define N_BOARD_QUADS VSX_N_ELEMENTS(board_quads)
#define N_BOARD_VERTICES (N_BOARD_QUADS * 4)

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
generate_vertices(struct vertex *vertices,
                  int corner_image,
                  const struct board_quad *quads,
                  size_t n_quads)
{
        struct vertex *v = vertices;

        uint16_t left = 96 * corner_image * 65535 / 256;
        uint16_t right = (96 * corner_image + 64) * 65535 / 256;
        uint16_t top = 0;
        uint16_t bottom = 65535;

        for (int i = 0; i < n_quads; i++) {
                const struct board_quad *quad = quads + i;

                v->x = quad->x1;
                v->y = quad->y1;
                v->s = quad->s1 ? right : left;
                v->t = quad->t1 ? bottom : top;
                v++;
                v->x = quad->x1;
                v->y = quad->y2;
                v->s = quad->s1 ? right : left;
                v->t = quad->t2 ? bottom : top;
                v++;
                v->x = quad->x2;
                v->y = quad->y1;
                v->s = quad->s2 ? right : left;
                v->t = quad->t1 ? bottom : top;
                v++;
                v->x = quad->x2;
                v->y = quad->y2;
                v->s = quad->s2 ? right : left;
                v->t = quad->t2 ? bottom : top;
                v++;
        }

        assert(v - vertices == n_quads * 4);
}

static void
create_buffer(struct vsx_board_painter *painter)
{
        size_t total_n_quads = N_BOARD_QUADS;

        for (int i = 0; i < VSX_N_ELEMENTS(player_boxes); i++)
                total_n_quads += player_boxes[i].n_quads * N_BOX_STYLES;

        size_t total_n_vertices = total_n_quads * 4;

        vsx_gl.glGenBuffers(1, &painter->vbo);
        vsx_gl.glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);
        vsx_gl.glBufferData(GL_ARRAY_BUFFER,
                            total_n_vertices * sizeof (struct vertex),
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
                                       GL_UNSIGNED_SHORT,
                                       true, /* normalized */
                                       sizeof (struct vertex),
                                       0, /* divisor */
                                       painter->vbo,
                                       offsetof(struct vertex, s));

        struct vertex *vertices =
                vsx_map_buffer_map(GL_ARRAY_BUFFER,
                                   total_n_vertices * sizeof (struct vertex),
                                   false, /* flush explicit */
                                   GL_STATIC_DRAW);

        struct vertex *v = vertices;

        generate_vertices(v,
                          0, /* corner_image */
                          board_quads,
                          N_BOARD_QUADS);
        v += N_BOARD_VERTICES;

        for (int player = 0; player < VSX_N_ELEMENTS(player_boxes); player++) {
                const struct player_box *box = player_boxes + player;

                for (int style = 0; style < N_BOX_STYLES; style++) {
                        struct box_style_draw_command *command =
                                painter->box_draw_commands[player].styles +
                                style;

                        command->offset = ((v - vertices) * 6 / 4 *
                                           sizeof (uint16_t));
                        command->min_vertex = v - vertices;
                        command->max_vertex = (v - vertices +
                                               box->n_quads * 4 -
                                               1);

                        generate_vertices(v, style, box->quads, box->n_quads);
                        v += box->n_quads * 4;
                }
        }

        assert(v - vertices == total_n_vertices);

        vsx_map_buffer_unmap();

        painter->element_buffer =
                vsx_quad_buffer_generate(painter->vao, total_n_quads);
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

struct paint_box_closure {
        struct vsx_board_painter *painter;
        int player_num;
};

static void
paint_box_cb(const char *name,
             enum vsx_game_state_player_flag flags,
             void *user_data)
{
        struct paint_box_closure *closure = user_data;

        assert(closure->player_num <= VSX_GAME_STATE_N_VISIBLE_PLAYERS);

        int player_num = closure->player_num;
        const struct box_draw_command *box =
                closure->painter->box_draw_commands + player_num;
        const struct box_style_draw_command *style;

        if ((flags & VSX_GAME_STATE_PLAYER_FLAG_NEXT_TURN))
                style = box->styles + 1;
        else
                style = box->styles + 0;

        vsx_gl_draw_range_elements(GL_TRIANGLES,
                                   style->min_vertex,
                                   style->max_vertex,
                                   player_boxes[player_num].n_quads * 6,
                                   GL_UNSIGNED_SHORT,
                                   (GLvoid *) (intptr_t)
                                   style->offset);

        closure->player_num++;
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
                                   0, N_BOARD_VERTICES - 1,
                                   N_BOARD_QUADS * 6,
                                   GL_UNSIGNED_SHORT,
                                   NULL /* indices */);

        struct paint_box_closure closure = {
                .player_num = 0,
                .painter = painter,
        };

        vsx_game_state_foreach_player(game_state, paint_box_cb, &closure);

        assert(closure.player_num == VSX_GAME_STATE_N_VISIBLE_PLAYERS);
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
