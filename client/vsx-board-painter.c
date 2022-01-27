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
#include "vsx-layout.h"

#define N_BOX_STYLES 3

struct box_style_draw_command {
        int offset;
        int min_vertex, max_vertex;
};

struct box_draw_command {
        struct box_style_draw_command styles[N_BOX_STYLES];
};

struct vsx_board_painter {
        struct vsx_game_state *game_state;
        struct vsx_listener modified_listener;

        struct vsx_toolbox *toolbox;

        struct box_draw_command box_draw_commands
        [VSX_GAME_STATE_N_VISIBLE_PLAYERS];

        struct vsx_layout_paint_position name_labels
        [VSX_GAME_STATE_N_VISIBLE_PLAYERS];
        bool name_label_positions_dirty;

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
        int center_x, center_y;
};

static const struct player_box
player_boxes[] = {
        {
                .n_quads = VSX_N_ELEMENTS(player_0_quads),
                .quads = player_0_quads,
                .center_x = (PLAYER_SPACE_MIDDLE_X +
                             PLAYER_SPACE_MIDDLE_WIDTH / 2),
                .center_y = PLAYER_SPACE_MIDDLE_HEIGHT / 2,
        },
        {
                .n_quads = VSX_N_ELEMENTS(player_1_quads),
                .quads = player_1_quads,
                .center_x = (PLAYER_SPACE_MIDDLE_X +
                             PLAYER_SPACE_MIDDLE_WIDTH / 2),
                .center_y = VSX_BOARD_HEIGHT - PLAYER_SPACE_MIDDLE_HEIGHT / 2,
        },
        {
                .n_quads = VSX_N_ELEMENTS(player_2_quads),
                .quads = player_2_quads,
                .center_x = PLAYER_SPACE_SIDE_WIDTH / 2,
                .center_y = PLAYER_SPACE_SIDE_HEIGHT / 2,
        },
        {
                .n_quads = VSX_N_ELEMENTS(player_3_quads),
                .quads = player_3_quads,
                .center_x = VSX_BOARD_WIDTH - PLAYER_SPACE_SIDE_WIDTH / 2,
                .center_y = PLAYER_SPACE_SIDE_HEIGHT / 2,
        },
        {
                .n_quads = VSX_N_ELEMENTS(player_4_quads),
                .quads = player_4_quads,
                .center_x = PLAYER_SPACE_SIDE_WIDTH / 2,
                .center_y = VSX_BOARD_HEIGHT - PLAYER_SPACE_SIDE_HEIGHT / 2,
        },
        {
                .n_quads = VSX_N_ELEMENTS(player_5_quads),
                .quads = player_5_quads,
                .center_x = VSX_BOARD_WIDTH - PLAYER_SPACE_SIDE_WIDTH / 2,
                .center_y = VSX_BOARD_HEIGHT - PLAYER_SPACE_SIDE_HEIGHT / 2,
        },
};

_Static_assert(VSX_N_ELEMENTS(player_boxes) == VSX_GAME_STATE_N_VISIBLE_PLAYERS,
               "The number of defined player boxes doesn’t match the number "
               "of visible players");

#define N_BOARD_QUADS VSX_N_ELEMENTS(board_quads)
#define N_BOARD_VERTICES (N_BOARD_QUADS * 4)

static void
update_player_name(struct vsx_board_painter *painter,
                   int player_num,
                   const char *name)
{
        struct vsx_layout *layout = painter->name_labels[player_num].layout;

        vsx_layout_set_text(layout, name);
}

static void
modified_cb(struct vsx_listener *listener,
            void *user_data)
{
        struct vsx_board_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_board_painter,
                                 modified_listener);
        const struct vsx_game_state_modified_event *event = user_data;

        switch (event->type) {
        case VSX_GAME_STATE_MODIFIED_TYPE_PLAYER_NAME:
                update_player_name(painter,
                                   event->player_name.player_num,
                                   event->player_name.name);
                painter->name_label_positions_dirty = true;
                vsx_signal_emit(&painter->redraw_needed_signal, NULL);
                break;
        case VSX_GAME_STATE_MODIFIED_TYPE_SHOUTING_PLAYER:
        case VSX_GAME_STATE_MODIFIED_TYPE_PLAYER_FLAGS:
                vsx_signal_emit(&painter->redraw_needed_signal, NULL);
                break;
        default:
                break;
        }
}

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

        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glGenTextures(1, &painter->tex);

        gl->glBindTexture(GL_TEXTURE_2D, painter->tex);
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

        vsx_mipmap_load_image(image, gl, painter->tex);

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

        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glGenBuffers(1, &painter->vbo);
        gl->glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);
        gl->glBufferData(GL_ARRAY_BUFFER,
                         total_n_vertices * sizeof (struct vertex),
                         NULL, /* data */
                         GL_STATIC_DRAW);

        painter->vao = vsx_array_object_new(gl);

        vsx_array_object_set_attribute(painter->vao,
                                       gl,
                                       VSX_SHADER_DATA_ATTRIB_POSITION,
                                       2, /* size */
                                       GL_SHORT,
                                       false, /* normalized */
                                       sizeof (struct vertex),
                                       0, /* divisor */
                                       painter->vbo,
                                       offsetof(struct vertex, x));
        vsx_array_object_set_attribute(painter->vao,
                                       gl,
                                       VSX_SHADER_DATA_ATTRIB_TEX_COORD,
                                       2, /* size */
                                       GL_UNSIGNED_SHORT,
                                       true, /* normalized */
                                       sizeof (struct vertex),
                                       0, /* divisor */
                                       painter->vbo,
                                       offsetof(struct vertex, s));

        struct vertex *vertices =
                vsx_map_buffer_map(painter->toolbox->map_buffer,
                                   GL_ARRAY_BUFFER,
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

        vsx_map_buffer_unmap(painter->toolbox->map_buffer);

        painter->element_buffer =
                vsx_quad_buffer_generate(painter->vao,
                                         gl,
                                         painter->toolbox->map_buffer,
                                         total_n_quads);
}

static void
load_name_cb(int player_num,
             const char *name,
             enum vsx_game_state_player_flag flags,
             void *user_data)
{
        struct vsx_board_painter *painter = user_data;

        if (name != NULL)
                update_player_name(painter, player_num, name);
}

static void *
create_cb(struct vsx_game_state *game_state,
          struct vsx_toolbox *toolbox)
{
        struct vsx_board_painter *painter = vsx_calloc(sizeof *painter);

        vsx_signal_init(&painter->redraw_needed_signal);

        painter->game_state = game_state;
        painter->toolbox = toolbox;

        for (int i = 0; i < VSX_GAME_STATE_N_VISIBLE_PLAYERS; i++)
                painter->name_labels[i].layout = vsx_layout_new(toolbox);

        create_buffer(painter);

        painter->image_token = vsx_image_loader_load(toolbox->image_loader,
                                                     "board.mpng",
                                                     texture_load_cb,
                                                     painter);

        painter->modified_listener.notify = modified_cb;
        vsx_signal_add(vsx_game_state_get_modified_signal(game_state),
                       &painter->modified_listener);

        vsx_game_state_foreach_player(game_state, load_name_cb, painter);

        painter->name_label_positions_dirty = true;

        return painter;
}

static void
paint_box_cb(int player_num,
             const char *name,
             enum vsx_game_state_player_flag flags,
             void *user_data)
{
        struct vsx_board_painter *painter = user_data;

        assert(player_num <= VSX_GAME_STATE_N_VISIBLE_PLAYERS);

        const struct box_draw_command *box =
                painter->box_draw_commands + player_num;
        const struct box_style_draw_command *style;

        int shouting_player =
                vsx_game_state_get_shouting_player(painter->game_state);

        if (player_num == shouting_player)
                style = box->styles + 2;
        else if ((flags & VSX_GAME_STATE_PLAYER_FLAG_NEXT_TURN))
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
}

static void
update_name_label_position(struct vsx_board_painter *painter,
                           int player_num)
{
        struct vsx_layout *layout = painter->name_labels[player_num].layout;

        const struct vsx_paint_state *paint_state =
                &painter->toolbox->paint_state;

        int board_top, board_left, board_width, board_height;

        if (paint_state->board_rotated) {
                board_left = (paint_state->height -
                              paint_state->board_scissor_y -
                              paint_state->board_scissor_height);
                board_top = (paint_state->width -
                             paint_state->board_scissor_x -
                             paint_state->board_scissor_width);

                board_width = paint_state->board_scissor_height;
                board_height = paint_state->board_scissor_width;
        } else {
                board_left = paint_state->board_scissor_x;
                board_top = (paint_state->height -
                              paint_state->board_scissor_y -
                              paint_state->board_scissor_height);

                board_width = paint_state->board_scissor_width;
                board_height = paint_state->board_scissor_height;
        }

        const struct player_box *box = player_boxes + player_num;

        int center_x = box->center_x * board_width / VSX_BOARD_WIDTH;
        int center_y = box->center_y * board_width / VSX_BOARD_WIDTH;

        const struct vsx_layout_extents *extents =
                vsx_layout_get_logical_extents(layout);

        int layout_x = center_x - (extents->right + extents->left) / 2;

        if (layout_x - extents->left < 0)
                layout_x = extents->left;
        else if (layout_x + extents->right > board_width)
                layout_x = board_width - extents->right;

        int layout_y = (center_y +
                        (extents->bottom + extents->top) / 2 -
                        extents->bottom);

        if (layout_y - extents->top < 0)
                layout_y = extents->top;
        else if (layout_y + extents->bottom > board_height)
                layout_y = board_height - extents->bottom;

        struct vsx_layout_paint_position *label =
                painter->name_labels + player_num;

        label->x = layout_x + board_left;
        label->y = layout_y + board_top;
}

static void
fb_size_changed_cb(void *painter_data)
{
        struct vsx_board_painter *painter = painter_data;

        painter->name_label_positions_dirty = true;
}

static void
prepare_cb(void *painter_data)
{
        struct vsx_board_painter *painter = painter_data;

        if (painter->name_label_positions_dirty) {
                vsx_paint_state_ensure_layout(&painter->toolbox->paint_state);

                for (int i = 0; i < VSX_GAME_STATE_N_VISIBLE_PLAYERS; i++) {
                        vsx_layout_prepare(painter->name_labels[i].layout);
                        update_name_label_position(painter, i);
                }

                painter->name_label_positions_dirty = false;
        }
}

static void
paint_cb(void *painter_data)
{
        struct vsx_board_painter *painter = painter_data;

        if (painter->tex == 0)
                return;

        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        vsx_paint_state_ensure_layout(paint_state);

        const struct vsx_shader_data *shader_data =
                &painter->toolbox->shader_data;
        const struct vsx_shader_data_program_data *program =
                shader_data->programs + VSX_SHADER_DATA_PROGRAM_TEXTURE;

        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glUseProgram(program->program);
        vsx_array_object_bind(painter->vao, gl);

        gl->glUniformMatrix2fv(program->matrix_uniform,
                               1, /* count */
                               GL_FALSE, /* transpose */
                               paint_state->board_matrix);
        gl->glUniform2f(program->translation_uniform,
                        paint_state->board_translation[0],
                        paint_state->board_translation[1]);

        gl->glBindTexture(GL_TEXTURE_2D, painter->tex);

        vsx_gl_draw_range_elements(GL_TRIANGLES,
                                   0, N_BOARD_VERTICES - 1,
                                   N_BOARD_QUADS * 6,
                                   GL_UNSIGNED_SHORT,
                                   NULL /* indices */);

        vsx_game_state_foreach_player(painter->game_state,
                                      paint_box_cb,
                                      painter);

        vsx_layout_paint_multiple(painter->name_labels,
                                  VSX_N_ELEMENTS(painter->name_labels),
                                  0.0f, 0.0f, 0.0f);
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

        vsx_list_remove(&painter->modified_listener.link);

        for (int i = 0; i < VSX_GAME_STATE_N_VISIBLE_PLAYERS; i++) {
                if (painter->name_labels[i].layout)
                        vsx_layout_free(painter->name_labels[i].layout);
        }

        struct vsx_gl *gl = painter->toolbox->gl;

        if (painter->vao)
                vsx_array_object_free(painter->vao, gl);
        if (painter->vbo)
                gl->glDeleteBuffers(1, &painter->vbo);
        if (painter->element_buffer)
                gl->glDeleteBuffers(1, &painter->element_buffer);

        if (painter->image_token)
                vsx_image_loader_cancel(painter->image_token);
        if (painter->tex)
                gl->glDeleteTextures(1, &painter->tex);

        vsx_free(painter);
}

const struct vsx_painter
vsx_board_painter = {
        .create_cb = create_cb,
        .fb_size_changed_cb = fb_size_changed_cb,
        .prepare_cb = prepare_cb,
        .paint_cb = paint_cb,
        .get_redraw_needed_signal_cb = get_redraw_needed_signal_cb,
        .free_cb = free_cb,
};
