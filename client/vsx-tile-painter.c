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
#include <limits.h>

#include "vsx-tile-texture.h"
#include "vsx-gl.h"
#include "vsx-board.h"
#include "vsx-buffer.h"
#include "vsx-slab.h"
#include "vsx-monotonic.h"

struct vsx_tile_painter_tile {
        int num;
        /* The position that the tile should be drawn at. This will
         * change as the tile is animated.
         */
        int16_t current_x, current_y;
        /* The start position of the animation */
        int16_t start_x, start_y;
        /* The end position of the animation */
        int16_t target_x, target_y;
        /* The last position reported by the server. This can be
         * different from the target position if the tile currently
         * has an override.
         */
        int16_t server_x, server_y;

        /* True if the tile has been manipulated by the user and it’s
         * target position has been overriden to be different from
         * what the server reported.
         */
        bool overridden;
        /* Link if the override list if the tile is overridden */
        struct vsx_list override_link;

        bool animating;
        int64_t animation_start_time;
        int64_t animation_end_time;

        const struct vsx_tile_texture_letter *letter_data;
        struct vsx_list link;
};

struct vsx_tile_painter {
        struct vsx_game_state *game_state;
        struct vsx_listener event_listener;
        struct vsx_listener modified_listener;

        struct vsx_toolbox *toolbox;

        struct vsx_listener tile_tool_ready_listener;

        struct vsx_tile_tool_buffer *tile_buffer;

        /* Array of tile pointers indexed by tile number */
        struct vsx_buffer tiles_by_index;
        /* List of tiles in reverse order of last updated */
        struct vsx_list tile_list;
        /* Slab allocator for the tiles */
        struct vsx_slab_allocator tile_allocator;

        /* Timeout that will clear all of the overides when fired. The
         * idea is that after this point the server will have had
         * enough time to process the update and if it hasn’t updated
         * the tile position before then then the manipulation hasn’t
         * worked and it’s better to revert back to the server’s
         * position.
         */
        struct vsx_main_thread_token *override_timeout;
        /* List of tiles that are currently overriden */
        struct vsx_list overrides;

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
};

/* We’ll pretend the tile is bigger than it is when looking for a tile
 * to process an input event in order to give the player a bigger area
 * to click on.
 */
#define INPUT_TILE_SIZE (VSX_BOARD_TILE_SIZE * 2)

/* The speed of tile animations measured in board units per second.
 *
 * 0.5 second to travel the width of the board.
 */
#define ANIMATION_SPEED (VSX_BOARD_WIDTH * 2)

/* Time in microseconds since the last override before reverting back
 * to what the server reported.
 */
#define OVERRIDE_TIMEOUT (3 * 1000 * 1000)

static void
remove_override_timeout(struct vsx_tile_painter *painter)
{
        if (painter->override_timeout == NULL)
                return;

        vsx_main_thread_cancel_idle(painter->override_timeout);
        painter->override_timeout = NULL;
}

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
cancel_override(struct vsx_tile_painter *painter,
                struct vsx_tile_painter_tile *tile)
{
        if (!tile->overridden)
                return;

        tile->overridden = false;
        vsx_list_remove(&tile->override_link);

        tile->target_x = tile->server_x;
        tile->target_y = tile->server_y;

        if (painter->dragging_tile == tile)
                painter->dragging_tile = NULL;

        start_animation(tile);
}

static void
cancel_all_overrides(struct vsx_tile_painter *painter)
{
        struct vsx_tile_painter_tile *tile, *tmp;

        vsx_list_for_each_safe(tile, tmp, &painter->overrides, override_link) {
                cancel_override(painter, tile);
        }

        assert(vsx_list_empty(&painter->overrides));
        assert(painter->dragging_tile == NULL);

        remove_override_timeout(painter);
}

static void
cancel_overrides_cb(void *user_data)
{
        struct vsx_tile_painter *painter = user_data;

        painter->override_timeout = NULL;

        if (!vsx_list_empty(&painter->overrides)) {
                cancel_all_overrides(painter);
                vsx_signal_emit(&painter->redraw_needed_signal, NULL);
        }
}

static void
set_override_timeout(struct vsx_tile_painter *painter)
{
        remove_override_timeout(painter);

        painter->override_timeout =
                vsx_main_thread_queue_timeout(painter->toolbox->main_thread,
                                              OVERRIDE_TIMEOUT,
                                              cancel_overrides_cb,
                                              painter);
}

static void
override_tile(struct vsx_tile_painter *painter,
              struct vsx_tile_painter_tile *tile)
{
        set_override_timeout(painter);

        if (tile->overridden)
                return;

        vsx_list_insert(&painter->overrides, &tile->override_link);
        tile->overridden = true;
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
                uint32_t letter = event->tile_changed.letter;
                tile->letter_data = vsx_tile_texture_find_letter(letter);
                tile->current_x = VSX_BOARD_WIDTH;
                tile->current_y = VSX_BOARD_HEIGHT / 4;
                tile->overridden = false;
        }

        tile->server_x = event->tile_changed.x;
        tile->server_y = event->tile_changed.y;

        int self = vsx_game_state_get_self(painter->game_state);

        if (tile->overridden) {
                if (event->tile_changed.last_player_moved != self) {
                        /* The tile has been moved by someone else
                         * while we were trying to manipulate it. Cancel the
                         * override.
                         */
                        cancel_override(painter, tile);
                }
        } else {
                tile->target_x = tile->server_x;
                tile->target_y = tile->server_y;

                if (event->synced) {
                        start_animation(tile);
                } else {
                        tile->animating = false;
                        tile->current_x = tile->target_x;
                        tile->current_y = tile->target_y;
                }
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
clear_tiles(struct vsx_tile_painter *painter)
{
        painter->snap_tile = NULL;
        cancel_all_overrides(painter);

        vsx_buffer_set_length(&painter->tiles_by_index, 0);
        vsx_list_init(&painter->tile_list);
        vsx_list_init(&painter->overrides);
        vsx_slab_destroy(&painter->tile_allocator);
        vsx_slab_init(&painter->tile_allocator);

        vsx_signal_emit(&painter->redraw_needed_signal, NULL);
}

static void
modified_cb(struct vsx_listener *listener,
            void *user_data)
{
        struct vsx_tile_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_tile_painter,
                                 modified_listener);
        const struct vsx_game_state_modified_event *event = user_data;

        switch (event->type) {
        case VSX_GAME_STATE_MODIFIED_TYPE_RESET:
                clear_tiles(painter);
                break;
        default:
                break;
        }
}

static void
tile_tool_ready_cb(struct vsx_listener *listener,
                   void *user_data)
{
        struct vsx_tile_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_tile_painter,
                                 tile_tool_ready_listener);

        if (get_n_tiles(painter) > 0)
                vsx_signal_emit(&painter->redraw_needed_signal, NULL);
}

static void
init_tiles_cb(const struct vsx_connection_event *event,
              void *user_data)
{
        struct vsx_tile_painter *painter = user_data;

        handle_tile_event(painter, event);
}

static void *
create_cb(struct vsx_game_state *game_state,
          struct vsx_toolbox *toolbox)
{
        struct vsx_tile_painter *painter = vsx_calloc(sizeof *painter);

        painter->game_state = game_state;
        painter->toolbox = toolbox;

        vsx_buffer_init(&painter->tiles_by_index);
        vsx_slab_init(&painter->tile_allocator);
        vsx_list_init(&painter->tile_list);
        vsx_list_init(&painter->overrides);

        vsx_signal_init(&painter->redraw_needed_signal);

        painter->tile_buffer =
                vsx_tile_tool_create_buffer(toolbox->tile_tool,
                                            VSX_BOARD_TILE_SIZE);

        painter->event_listener.notify = event_cb;
        vsx_signal_add(vsx_game_state_get_event_signal(game_state),
                       &painter->event_listener);

        painter->modified_listener.notify = modified_cb;
        vsx_signal_add(vsx_game_state_get_modified_signal(game_state),
                       &painter->modified_listener);

        painter->tile_tool_ready_listener.notify = tile_tool_ready_cb;
        vsx_signal_add(vsx_tile_tool_get_ready_signal(toolbox->tile_tool),
                       &painter->tile_tool_ready_listener);

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

        struct vsx_tile_painter_tile *best_tile = NULL;
        struct vsx_tile_painter_tile *tile;

        int best_distance_2 = INT_MAX;

        vsx_list_for_each_reverse(tile, &painter->tile_list, link) {
                int tile_center_x = tile->current_x + VSX_BOARD_TILE_SIZE / 2;
                int tile_center_y = tile->current_y + VSX_BOARD_TILE_SIZE / 2;
                int dx = abs(board_x - tile_center_x);
                int dy = abs(board_y - tile_center_y);

                if (dx > INPUT_TILE_SIZE / 2 || dy > INPUT_TILE_SIZE / 2)
                        continue;

                /* If the click is actually on a tile then we’ll use
                 * it straight away so that it will always use the
                 * topmost one.
                 */
                if (dx <= VSX_BOARD_TILE_SIZE / 2 &&
                    dy <= VSX_BOARD_TILE_SIZE / 2)
                        return tile;

                /* Calculate the distance squared */
                int distance_2 = dx * dx + dy * dy;
                /* Pick the closest tile */
                if (distance_2 < best_distance_2) {
                        best_distance_2 = distance_2;
                        best_tile = tile;
                }

        }

        return best_tile;
}

static bool
is_other_shouting(struct vsx_tile_painter *painter)
{
        int shouting_player =
                vsx_game_state_get_shouting_player(painter->game_state);

        if (shouting_player == -1)
                return false;

        return shouting_player != vsx_game_state_get_self(painter->game_state);
}

static bool
handle_click(struct vsx_tile_painter *painter,
             const struct vsx_input_event *event)
{
        if (is_other_shouting(painter))
                return false;

        if (painter->snap_tile == NULL)
                return false;

        int snap_x = painter->snap_x;
        int snap_y = painter->snap_y;

        if (snap_x < 0 || snap_y < 0 ||
            snap_x + VSX_BOARD_TILE_SIZE > VSX_BOARD_WIDTH ||
            snap_y + VSX_BOARD_TILE_SIZE > VSX_BOARD_HEIGHT) {
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

        tile->target_x = painter->snap_x;
        tile->target_y = painter->snap_y;

        painter->snap_x += VSX_BOARD_TILE_SIZE;

        override_tile(painter, tile);
        start_animation(tile);
        raise_tile(painter, tile);
        vsx_signal_emit(&painter->redraw_needed_signal, NULL);

        return true;
}

static bool
handle_drag_start(struct vsx_tile_painter *painter,
                  const struct vsx_input_event *event)
{
        if (is_other_shouting(painter)) {
                painter->dragging_tile = NULL;
                return false;
        }

        int board_x, board_y;

        screen_coord_to_board(&painter->toolbox->paint_state,
                              event->drag.x, event->drag.y,
                              &board_x, &board_y);

        struct vsx_tile_painter_tile *tile =
                find_tile_at_pos(painter, board_x, board_y);

        if (tile == NULL) {
                painter->dragging_tile = NULL;
                return false;
        }

        override_tile(painter, tile);

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

        /* Ignore the dragging until we’ll sure that it’s not just
         * going to be a click.
         */
        if (event->drag.maybe_click)
                return true;

        int board_x, board_y;

        screen_coord_to_board(&painter->toolbox->paint_state,
                              event->drag.x, event->drag.y,
                              &board_x, &board_y);

        if (board_x < 0 || board_x >= VSX_BOARD_WIDTH ||
            board_y < 0 || board_y >= VSX_BOARD_HEIGHT)
                return true;

        int new_x = board_x + painter->drag_offset_x;
        int new_y = board_y + painter->drag_offset_y;

        if (new_x < 0)
                new_x = 0;
        else if (new_x + VSX_BOARD_TILE_SIZE > VSX_BOARD_WIDTH)
                new_x = VSX_BOARD_WIDTH - VSX_BOARD_TILE_SIZE;

        if (new_y < 0)
                new_y = 0;
        else if (new_y + VSX_BOARD_TILE_SIZE > VSX_BOARD_HEIGHT)
                new_y = VSX_BOARD_HEIGHT - VSX_BOARD_TILE_SIZE;

        if (tile->current_x == new_x && tile->current_y == new_y)
                return true;

        tile->current_x = new_x;
        tile->current_y = new_y;

        painter->snap_tile = tile;
        painter->snap_x = tile->current_x + VSX_BOARD_TILE_SIZE;
        painter->snap_y = tile->current_y;

        override_tile(painter, tile);
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

static size_t
update_tile_vertices(struct vsx_tile_painter *painter,
                     size_t max_tiles)
{
        size_t n_quads = 0;

        struct vsx_tile_painter_tile *tile;

        vsx_tile_tool_begin_update(painter->tile_buffer, max_tiles);

        vsx_list_for_each(tile, &painter->tile_list, link) {
                if (tile->letter_data == NULL)
                        continue;

                vsx_tile_tool_add_tile(painter->tile_buffer,
                                       tile->current_x,
                                       tile->current_y,
                                       tile->letter_data);

                n_quads++;
        }

        vsx_tile_tool_end_update(painter->tile_buffer);

        return n_quads;
}

static void
paint_cb(void *painter_data)
{
        struct vsx_tile_painter *painter = painter_data;

        if (!vsx_tile_tool_is_ready(painter->toolbox->tile_tool))
                return;

        int n_tiles = get_n_tiles(painter);

        if (n_tiles <= 0)
                return;

        /* Cancel any overrides if another player started shouting
         * before the server heard about our attempt. That way the
         * tile will snap back to where the server last reported it to
         * be.
         */
        if (is_other_shouting(painter))
                cancel_all_overrides(painter);

        bool any_tiles_animating = update_tile_animations(painter);

        size_t n_quads = update_tile_vertices(painter, n_tiles);

        /* This shouldn’t happen unless for some reason all of the
         * tiles that the server sent had letters that we don’t
         * recognise.
         */
        if (n_quads <= 0)
                return;

        struct vsx_gl *gl = painter->toolbox->gl;

        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        vsx_paint_state_ensure_layout(paint_state);

        gl->glEnable(GL_SCISSOR_TEST);
        gl->glScissor(paint_state->board_scissor_x,
                      paint_state->board_scissor_y,
                      paint_state->board_scissor_width,
                      paint_state->board_scissor_height);

        vsx_tile_tool_paint(painter->tile_buffer,
                            &painter->toolbox->shader_data,
                            paint_state->board_matrix,
                            paint_state->board_translation);

        gl->glDisable(GL_SCISSOR_TEST);

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

        remove_override_timeout(painter);

        vsx_list_remove(&painter->event_listener.link);
        vsx_list_remove(&painter->modified_listener.link);
        vsx_list_remove(&painter->tile_tool_ready_listener.link);

        vsx_tile_tool_free_buffer(painter->tile_buffer);

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
