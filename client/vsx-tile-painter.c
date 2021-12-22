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
#include <assert.h>

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

        /* The tile that is currently being dragged or -1 if there is no tile */
        int dragging_tile;
        /* The game_state time counter value of when the dragging started */
        uint32_t dragging_start_time;
        /* The offset to add to the cursor board position to get the
         * topleft of the tile.
         */
        int drag_offset_x, drag_offset_y;
        /* The position that we last dragged the tile to so that we
         * can paint at this position without having to wait for the
         * server to tell us about it.
         */
        int drag_board_x, drag_board_y;

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
screen_coord_to_board(struct vsx_paint_state *paint_state,
                      int screen_x, int screen_y,
                      int *board_x, int *board_y)
{
        vsx_paint_state_ensure_layout(paint_state);

        if (paint_state->board_scissor_width == 0 ||
            paint_state->board_scissor_height == 0) {
                *board_x = -1;
                *board_y = -1;
        } else if (paint_state->board_rotated) {
                *board_x = ((screen_y -
                             (paint_state->height -
                              paint_state->board_scissor_y -
                              paint_state->board_scissor_height)) *
                            VSX_BOARD_WIDTH /
                            paint_state->board_scissor_height);
                *board_y = ((paint_state->board_scissor_width - 1 -
                             (screen_x - paint_state->board_scissor_x)) *
                            VSX_BOARD_HEIGHT /
                            paint_state->board_scissor_width);
        } else {
                *board_x = ((screen_x - paint_state->board_scissor_x) *
                            VSX_BOARD_WIDTH /
                            paint_state->board_scissor_width);
                *board_y = ((screen_y -
                             (paint_state->height -
                              paint_state->board_scissor_y -
                              paint_state->board_scissor_height)) *
                            VSX_BOARD_HEIGHT /
                            paint_state->board_scissor_height);
        }
}

struct drag_start_tile_closure {
        struct vsx_tile_painter *painter;
        int board_x, board_y;
};

static void
drag_start_tile_cb(const struct vsx_game_state_tile *tile,
                   void *user_data)
{
        struct drag_start_tile_closure *closure = user_data;
        struct vsx_tile_painter *painter = closure->painter;

        if (closure->board_x < tile->x ||
            closure->board_x >= tile->x + TILE_SIZE ||
            closure->board_y < tile->y ||
            closure->board_y >= tile->y + TILE_SIZE)
                return;

        painter->dragging_tile = tile->number;
        painter->drag_offset_x = tile->x - closure->board_x;
        painter->drag_offset_y = tile->y - closure->board_y;
        painter->dragging_start_time =
                vsx_game_state_get_time_counter(painter->game_state);
        painter->drag_board_x = tile->x;
        painter->drag_board_y = tile->y;
}

static bool
handle_drag_start(struct vsx_tile_painter *painter,
                  const struct vsx_input_event *event)
{
        if (vsx_game_state_get_shout_state(painter->game_state) ==
            VSX_GAME_STATE_SHOUT_STATE_OTHER)
                return false;

        struct drag_start_tile_closure closure = {
                .painter = painter,
        };

        screen_coord_to_board(&painter->toolbox->paint_state,
                              event->drag.x, event->drag.y,
                              &closure.board_x, &closure.board_y);

        painter->dragging_tile = -1;

        if (closure.board_x < 0 || closure.board_x >= VSX_BOARD_WIDTH ||
            closure.board_y < 0 || closure.board_y >= VSX_BOARD_HEIGHT)
                return false;

        vsx_game_state_foreach_tile(painter->game_state,
                                    drag_start_tile_cb,
                                    &closure);

        return painter->dragging_tile != -1;
}

static bool
handle_drag(struct vsx_tile_painter *painter,
            const struct vsx_input_event *event)
{
        if (painter->dragging_tile == -1)
                return false;

        int board_x, board_y;

        screen_coord_to_board(&painter->toolbox->paint_state,
                              event->drag.x, event->drag.y,
                              &board_x, &board_y);

        if (board_x < 0 || board_x >= VSX_BOARD_WIDTH ||
            board_y < 0 || board_y >= VSX_BOARD_HEIGHT)
                return true;

        painter->drag_board_x = board_x + painter->drag_offset_x;
        painter->drag_board_y = board_y + painter->drag_offset_y;

        vsx_game_state_move_tile(painter->game_state,
                                 painter->dragging_tile,
                                 painter->drag_board_x,
                                 painter->drag_board_y);

        vsx_signal_emit(&painter->redraw_needed_signal, NULL);

        return true;
}

static bool
input_event_cb(void *painter_data,
               const struct vsx_input_event *event)
{
        struct vsx_tile_painter *painter = painter_data;

        switch (event->type) {
        case VSX_INPUT_EVENT_TYPE_CLICK:
        case VSX_INPUT_EVENT_TYPE_ZOOM_START:
        case VSX_INPUT_EVENT_TYPE_ZOOM:
                return false;

        case VSX_INPUT_EVENT_TYPE_DRAG_START:
                return handle_drag_start(painter, event);
        case VSX_INPUT_EVENT_TYPE_DRAG:
                return handle_drag(painter, event);
        }

        return false;
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
        struct vsx_tile_painter *painter;
        struct vertex *vertices;
        int quad_num;
        const struct vsx_tile_texture_letter *dragged_letter_data;
};

static void
store_tile_quad(struct tile_closure *closure,
                int tile_x, int tile_y,
                const struct vsx_tile_texture_letter *letter_data)
{
        struct vertex *v = closure->vertices + closure->quad_num * 4;

        v->x = tile_x;
        v->y = tile_y;
        v->s = letter_data->s1;
        v->t = letter_data->t1;
        v++;
        v->x = tile_x;
        v->y = tile_y + TILE_SIZE;
        v->s = letter_data->s1;
        v->t = letter_data->t2;
        v++;
        v->x = tile_x + TILE_SIZE;
        v->y = tile_y;
        v->s = letter_data->s2;
        v->t = letter_data->t1;
        v++;
        v->x = tile_x + TILE_SIZE;
        v->y = tile_y + TILE_SIZE;
        v->s = letter_data->s2;
        v->t = letter_data->t2;
        v++;

        closure->quad_num++;
}

static void
tile_cb(const struct vsx_game_state_tile *tile,
        void *user_data)
{
        struct tile_closure *closure = user_data;
        struct vsx_tile_painter *painter = closure->painter;

        const struct vsx_tile_texture_letter *letter_data =
                find_letter(tile->letter);

        if (letter_data == NULL)
                return;

        if (tile->number == painter->dragging_tile) {
                if (!tile->last_moved_by_self &&
                    tile->update_time > painter->dragging_start_time) {
                        /* The tile has been moved by someone else
                         * while we were trying to drag it. Cancel the
                         * drag.
                         */
                        painter->dragging_tile = -1;
                } else {
                        closure->dragged_letter_data = letter_data;
                        return;
                }
        }

        store_tile_quad(closure, tile->x, tile->y, letter_data);
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

        /* Cancel any running drag if another player started shouting
         * before the server heard about our attempt. That way the
         * tile will snap back to where the server last reported it to
         * be.
         */
        if (vsx_game_state_get_shout_state(painter->game_state) ==
            VSX_GAME_STATE_SHOUT_STATE_OTHER)
                painter->dragging_tile = -1;

        ensure_buffer_size(painter, n_tiles);

        struct tile_closure closure = {
                .painter = painter,
                .quad_num = 0,
                .dragged_letter_data = NULL,
        };

        vsx_gl.glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);

        closure.vertices = vsx_map_buffer_map(GL_ARRAY_BUFFER,
                                              painter->buffer_n_tiles *
                                              4 * sizeof (struct vertex),
                                              true, /* flush_explicit */
                                              GL_DYNAMIC_DRAW);

        vsx_game_state_foreach_tile(painter->game_state, tile_cb, &closure);

        /* Paint the dragged tile last so that it will always be above
         * the others.
         */
        if (closure.dragged_letter_data) {
                store_tile_quad(&closure,
                                painter->drag_board_x,
                                painter->drag_board_y,
                                closure.dragged_letter_data);
        }

        assert(closure.quad_num <= n_tiles);

        vsx_map_buffer_flush(0, closure.quad_num * 4 * sizeof (struct vertex));

        vsx_map_buffer_unmap();

        /* This shouldn’t happen unless for some reason all of the
         * tiles that the server sent had letters that we don’t
         * recognise.
         */
        if (closure.quad_num <= 0)
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
                                   0, closure.quad_num * 4 - 1,
                                   closure.quad_num * 6,
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
        .input_event_cb = input_event_cb,
        .get_redraw_needed_signal_cb = get_redraw_needed_signal_cb,
        .free_cb = free_cb,
};
