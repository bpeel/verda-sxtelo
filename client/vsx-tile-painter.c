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
#include <string.h>
#include <stdalign.h>

#include "vsx-map-buffer.h"
#include "vsx-quad-buffer.h"
#include "vsx-tile-texture.h"
#include "vsx-mipmap.h"
#include "vsx-gl.h"
#include "vsx-board.h"
#include "vsx-buffer.h"
#include "vsx-slab.h"
#include "vsx-monotonic.h"

struct vsx_tile_painter_tile {
        int num;
        int16_t current_x, current_y;
        int16_t start_x, start_y;
        int16_t target_x, target_y;

        bool animating;
        int64_t animation_start_time;
        int64_t animation_end_time;

        const struct vsx_tile_texture_letter *letter_data;
        struct vsx_list link;
};

struct vsx_tile_painter {
        struct vsx_game_state *game_state;
        struct vsx_listener event_listener;

        struct vsx_painter_toolbox *toolbox;

        GLuint program;
        GLint matrix_uniform;
        GLint translation_uniform;

        struct vsx_array_object *vao;
        GLuint vbo;
        GLuint element_buffer;

        GLuint tex;
        struct vsx_image_loader_token *image_token;

        /* Array of tile pointers indexed by tile number */
        struct vsx_buffer tiles_by_index;
        /* List of tiles in reverse order of last updated */
        struct vsx_list tile_list;
        /* Slab allocator for the tiles */
        struct vsx_slab_allocator tile_allocator;

        /* The tile that is currently being dragged or NULL if there
         * is no tile.
         */
        struct vsx_tile_painter_tile *dragging_tile;
        /* The offset to add to the cursor board position to get the
         * topleft of the tile.
         */
        int drag_offset_x, drag_offset_y;

        /* Tile that we will move next to if any other tile is
         * clicked, or NULL if no snap position is known. The position
         * is stored separately from the tile so that we don’t have to
         * wait for the animation to finish or for the server to
         * report the correct place before snapping another tile.
         */
        struct vsx_tile_painter_tile *snap_tile;
        int snap_x, snap_y;

        struct vsx_signal redraw_needed_signal;

        int buffer_n_tiles;
};

struct vertex {
        float x, y;
        uint16_t s, t;
};

#define TILE_SIZE 20

/* The speed of tile animations measured in board units per second.
 *
 * 0.5 second to travel the width of the board.
 */
#define ANIMATION_SPEED (VSX_BOARD_WIDTH * 2)

static void
ensure_n_tiles(struct vsx_tile_painter *painter,
               int n_tiles)
{
        size_t new_length =
                sizeof (struct vsx_tile_painter_tile *) * n_tiles;
        size_t old_length = painter->tiles_by_index.length;

        if (new_length > old_length) {
                vsx_buffer_set_length(&painter->tiles_by_index, new_length);
                memset(painter->tiles_by_index.data + old_length,
                       0,
                       new_length - old_length);
        }
}

static struct vsx_tile_painter_tile *
get_tile_by_index(struct vsx_tile_painter *painter,
                  int tile_num,
                  bool *is_new)
{
        ensure_n_tiles(painter, tile_num + 1);

        struct vsx_tile_painter_tile **tile_pointers =
                (struct vsx_tile_painter_tile **)
                painter->tiles_by_index.data;

        struct vsx_tile_painter_tile *tile = tile_pointers[tile_num];

        if (tile == NULL) {
                size_t alignment = alignof(struct vsx_tile_painter_tile);

                tile = vsx_slab_allocate(&painter->tile_allocator,
                                         sizeof *tile,
                                         alignment);
                tile_pointers[tile_num] = tile;
                vsx_list_insert(painter->tile_list.prev, &tile->link);

                tile->num = tile_num;

                *is_new = true;
        } else {
                *is_new = false;
        }

        return tile;
}

static size_t
get_n_tiles(struct vsx_tile_painter *painter)
{
        return (painter->tiles_by_index.length /
                sizeof (struct vsx_tile_painter_tile *));
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

static void
start_animation(struct vsx_tile_painter_tile *tile)
{
        if (tile->current_x != tile->target_x ||
            tile->current_y != tile->target_y) {
                tile->animating = true;
                tile->animation_start_time = vsx_monotonic_get();

                tile->start_x = tile->current_x;
                tile->start_y = tile->current_y;

                int dx = tile->start_x - tile->target_x;
                int dy = tile->start_y - tile->target_y;

                float animation_distance = sqrtf(dx * dx + dy * dy);

                int64_t animation_ms = roundf(animation_distance *
                                              1000.0f /
                                              ANIMATION_SPEED);

                tile->animation_end_time =
                        tile->animation_start_time + animation_ms * 1000;
        } else {
                tile->animating = false;
        }
}

static void
cancel_drag(struct vsx_tile_painter *painter)
{
        struct vsx_tile_painter_tile *tile = painter->dragging_tile;

        if (tile == NULL)
                return;

        start_animation(tile);

        painter->dragging_tile = NULL;
}

static void
raise_tile(struct vsx_tile_painter *painter,
           struct vsx_tile_painter_tile *tile)
{
        /* Move the tile to the end of the list so that it will be
         * drawn last.
         */
        vsx_list_remove(&tile->link);
        vsx_list_insert(painter->tile_list.prev, &tile->link);
}

static void
handle_tile_event(struct vsx_tile_painter *painter,
                  const struct vsx_connection_event *event)
{
        bool is_new;
        struct vsx_tile_painter_tile *tile =
                get_tile_by_index(painter, event->tile_changed.num, &is_new);

        if (is_new) {
                tile->letter_data = find_letter(event->tile_changed.letter);
                tile->current_x = VSX_BOARD_WIDTH;
                tile->current_y = VSX_BOARD_HEIGHT / 4;
        }

        tile->target_x = event->tile_changed.x;
        tile->target_y = event->tile_changed.y;

        int self = vsx_game_state_get_self(painter->game_state);

        if (tile == painter->dragging_tile) {
                if (event->tile_changed.last_player_moved != self) {
                        /* The tile has been moved by someone else
                         * while we were trying to drag it. Cancel the
                         * drag.
                         */
                        cancel_drag(painter);
                }
        } else if (event->synced) {
                start_animation(tile);
        } else {
                tile->animating = false;
                tile->current_x = tile->target_x;
                tile->current_y = tile->target_y;
        }

        if (is_new || event->tile_changed.last_player_moved != self)
                painter->snap_tile = NULL;

        raise_tile(painter, tile);

        vsx_signal_emit(&painter->redraw_needed_signal, NULL);
}

static void
event_cb(struct vsx_listener *listener,
         void *user_data)
{
        struct vsx_tile_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_tile_painter,
                                 event_listener);
        const struct vsx_connection_event *event = user_data;

        switch (event->type) {
        case VSX_CONNECTION_EVENT_TYPE_TILE_CHANGED:
                handle_tile_event(painter, event);
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
init_tiles_cb(const struct vsx_connection_event *event,
              void *user_data)
{
        struct vsx_tile_painter *painter = user_data;

        handle_tile_event(painter, event);
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

        vsx_buffer_init(&painter->tiles_by_index);
        vsx_slab_init(&painter->tile_allocator);
        vsx_list_init(&painter->tile_list);

        vsx_signal_init(&painter->redraw_needed_signal);

        init_program(painter, &toolbox->shader_data);

        painter->image_token = vsx_image_loader_load(toolbox->image_loader,
                                                     "tiles.mpng",
                                                     texture_load_cb,
                                                     painter);

        painter->event_listener.notify = event_cb;
        vsx_signal_add(vsx_game_state_get_event_signal(game_state),
                       &painter->event_listener);

        vsx_game_state_foreach_tile(painter->game_state,
                                    init_tiles_cb,
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

static struct vsx_tile_painter_tile *
find_tile_at_pos(struct vsx_tile_painter *painter,
                 int board_x, int board_y)
{
        if (board_x < 0 || board_x >= VSX_BOARD_WIDTH ||
            board_y < 0 || board_y >= VSX_BOARD_HEIGHT)
                return NULL;

        struct vsx_tile_painter_tile *found_tile = NULL;
        struct vsx_tile_painter_tile *tile;

        vsx_list_for_each(tile, &painter->tile_list, link) {
                if (board_x < tile->current_x ||
                    board_x >= tile->current_x + TILE_SIZE ||
                    board_y < tile->current_y ||
                    board_y >= tile->current_y + TILE_SIZE)
                        continue;

                found_tile = tile;
        }

        return found_tile;
}

static bool
handle_click(struct vsx_tile_painter *painter,
             const struct vsx_input_event *event)
{
        cancel_drag(painter);

        if (painter->snap_tile == NULL)
                return false;

        int snap_x = painter->snap_x;
        int snap_y = painter->snap_y;

        if (snap_x < 0 || snap_y < 0 ||
            snap_x + TILE_SIZE > VSX_BOARD_WIDTH ||
            snap_y + TILE_SIZE > VSX_BOARD_HEIGHT) {
                painter->snap_tile = NULL;
                return false;
        }

        int board_x, board_y;

        screen_coord_to_board(&painter->toolbox->paint_state,
                              event->click.x, event->click.y,
                              &board_x, &board_y);

        struct vsx_tile_painter_tile *tile =
                find_tile_at_pos(painter, board_x, board_y);

        if (tile == NULL || tile == painter->snap_tile)
                return false;

        painter->snap_tile = tile;
        vsx_game_state_move_tile(painter->game_state,
                                 tile->num,
                                 painter->snap_x,
                                 painter->snap_y);
        painter->snap_x += TILE_SIZE;

        return true;
}

static bool
handle_drag_start(struct vsx_tile_painter *painter,
                  const struct vsx_input_event *event)
{
        if (vsx_game_state_get_shout_state(painter->game_state) ==
            VSX_GAME_STATE_SHOUT_STATE_OTHER)
                return false;

        int board_x, board_y;

        screen_coord_to_board(&painter->toolbox->paint_state,
                              event->drag.x, event->drag.y,
                              &board_x, &board_y);

        cancel_drag(painter);

        struct vsx_tile_painter_tile *tile =
                find_tile_at_pos(painter, board_x, board_y);

        if (tile == NULL)
                return false;

        painter->dragging_tile = tile;
        painter->drag_offset_x = tile->current_x - board_x;
        painter->drag_offset_y = tile->current_y - board_y;
        tile->animating = false;
        raise_tile(painter, tile);
        vsx_signal_emit(&painter->redraw_needed_signal, NULL);

        return true;
}

static bool
handle_drag(struct vsx_tile_painter *painter,
            const struct vsx_input_event *event)
{
        struct vsx_tile_painter_tile *tile = painter->dragging_tile;

        if (tile == NULL)
                return false;

        int board_x, board_y;

        screen_coord_to_board(&painter->toolbox->paint_state,
                              event->drag.x, event->drag.y,
                              &board_x, &board_y);

        if (board_x < 0 || board_x >= VSX_BOARD_WIDTH ||
            board_y < 0 || board_y >= VSX_BOARD_HEIGHT)
                return true;

        tile->current_x = board_x + painter->drag_offset_x;
        tile->current_y = board_y + painter->drag_offset_y;

        painter->snap_tile = tile;
        painter->snap_x = tile->current_x + TILE_SIZE;
        painter->snap_y = tile->current_y;

        raise_tile(painter, painter->dragging_tile);

        vsx_game_state_move_tile(painter->game_state,
                                 painter->dragging_tile->num,
                                 tile->current_x,
                                 tile->current_y);

        vsx_signal_emit(&painter->redraw_needed_signal, NULL);

        return true;
}

static bool
input_event_cb(void *painter_data,
               const struct vsx_input_event *event)
{
        struct vsx_tile_painter *painter = painter_data;

        switch (event->type) {
        case VSX_INPUT_EVENT_TYPE_ZOOM_START:
        case VSX_INPUT_EVENT_TYPE_ZOOM:
                return false;

        case VSX_INPUT_EVENT_TYPE_CLICK:
                return handle_click(painter, event);
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

static int
interpolate_animation(int start_pos, int end_pos,
                      int64_t start_time, int64_t end_time,
                      int64_t now)
{
        return (start_pos +
                (now - start_time) *
                (end_pos - start_pos) /
                (end_time - start_time));
}

static bool
update_tile_animations(struct vsx_tile_painter *painter)
{
        bool any_tiles_animating = false;
        int64_t now = vsx_monotonic_get();

        struct vsx_tile_painter_tile *tile;

        vsx_list_for_each(tile, &painter->tile_list, link) {
                if (!tile->animating)
                        continue;

                if (now >= tile->animation_end_time) {
                        tile->animating = false;
                        tile->current_x = tile->target_x;
                        tile->current_y = tile->target_y;
                        continue;
                }

                int64_t start_time = tile->animation_start_time;
                int64_t end_time = tile->animation_end_time;

                tile->current_x = interpolate_animation(tile->start_x,
                                                        tile->target_x,
                                                        start_time,
                                                        end_time,
                                                        now);
                tile->current_y = interpolate_animation(tile->start_y,
                                                        tile->target_y,
                                                        start_time,
                                                        end_time,
                                                        now);

                any_tiles_animating = true;
        }

        return any_tiles_animating;
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

static struct vertex *
store_tile_quad(struct vertex *vertices,
                int tile_x, int tile_y,
                const struct vsx_tile_texture_letter *letter_data)
{
        struct vertex *v = vertices;

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

        return v;
}

static size_t
generate_tile_vertices(struct vsx_tile_painter *painter,
                       struct vertex *vertices)
{
        struct vertex *v = vertices;
        struct vsx_tile_painter_tile *tile;

        vsx_list_for_each(tile, &painter->tile_list, link) {
                if (tile->letter_data == NULL)
                        continue;

                v = store_tile_quad(v,
                                    tile->current_x,
                                    tile->current_y,
                                    tile->letter_data);
        }

        return v - vertices;
}

static void
paint_cb(void *painter_data)
{
        struct vsx_tile_painter *painter = painter_data;

        if (painter->tex == 0)
                return;

        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        vsx_paint_state_ensure_layout(paint_state);

        int n_tiles = get_n_tiles(painter);

        if (n_tiles <= 0)
                return;

        /* Cancel any running drag if another player started shouting
         * before the server heard about our attempt. That way the
         * tile will snap back to where the server last reported it to
         * be.
         */
        if (vsx_game_state_get_shout_state(painter->game_state) ==
            VSX_GAME_STATE_SHOUT_STATE_OTHER)
                cancel_drag(painter);

        bool any_tiles_animating = update_tile_animations(painter);

        ensure_buffer_size(painter, n_tiles);

        vsx_gl.glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);

        struct vertex *vertices =
                vsx_map_buffer_map(GL_ARRAY_BUFFER,
                                   painter->buffer_n_tiles *
                                   4 * sizeof (struct vertex),
                                   true, /* flush_explicit */
                                   GL_DYNAMIC_DRAW);

        size_t n_vertices = generate_tile_vertices(painter, vertices);
        size_t n_quads = n_vertices / 4;

        assert(n_quads <= n_tiles);

        vsx_map_buffer_flush(0, n_vertices * sizeof (struct vertex));

        vsx_map_buffer_unmap();

        /* This shouldn’t happen unless for some reason all of the
         * tiles that the server sent had letters that we don’t
         * recognise.
         */
        if (n_vertices <= 0)
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
                                   0, n_vertices - 1,
                                   n_quads * 6,
                                   GL_UNSIGNED_SHORT,
                                   NULL /* indices */);

        vsx_gl.glDisable(GL_SCISSOR_TEST);

        if (any_tiles_animating)
                vsx_signal_emit(&painter->redraw_needed_signal, NULL);
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

        vsx_buffer_destroy(&painter->tiles_by_index);
        vsx_slab_destroy(&painter->tile_allocator);

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
