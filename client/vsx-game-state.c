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

#include "vsx-game-state.h"

#include <pthread.h>
#include <strings.h>
#include <string.h>
#include <stdalign.h>

#include "vsx-util.h"
#include "vsx-signal.h"
#include "vsx-buffer.h"
#include "vsx-bitmask.h"
#include "vsx-list.h"
#include "vsx-slab.h"

struct vsx_game_state_player {
        char *name;
        enum vsx_game_state_player_flag flags;
};

struct vsx_game_state_tile {
        struct vsx_list link;
        int16_t x, y;
        uint32_t letter;
};

struct vsx_game_state {
        /* This data is only accessed from the main thread and doesn’t
         * need a mutex.
         */

        struct vsx_game_state_player players[VSX_GAME_STATE_N_VISIBLE_PLAYERS];

        /* Array of tile pointers indexed by tile number */
        struct vsx_buffer tiles_by_index;
        /* List of tiles in reverse order of last updated */
        struct vsx_list tile_list;
        /* Slab allocator for the tiles */
        struct vsx_slab_allocator tile_allocator;

        struct vsx_worker *worker;
        struct vsx_connection *connection;
        struct vsx_listener event_listener;

        pthread_mutex_t mutex;

        /* The following data is protected by the mutex and can be
         * modified from any thread.
         */

        uint32_t dirty_player_names;
        uint32_t dirty_player_flags;

        struct vsx_buffer dirty_tiles;
};

static void
update_player_names_locked(struct vsx_game_state *game_state)
{
        int bit;

        while ((bit = ffs(game_state->dirty_player_names))) {
                int player_num = bit - 1;

                const struct vsx_player *player =
                        vsx_connection_get_player(game_state->connection,
                                                  player_num);

                vsx_free(game_state->players[player_num].name);

                const char *new_name = vsx_player_get_name(player);

                game_state->players[player_num].name =
                        new_name ? vsx_strdup(new_name) : NULL;

                game_state->dirty_player_names &= ~(1 << player_num);
        }
}

static void
update_player_flags_locked(struct vsx_game_state *game_state)
{
        int bit;

        while ((bit = ffs(game_state->dirty_player_flags))) {
                int player_num = bit - 1;

                const struct vsx_player *player =
                        vsx_connection_get_player(game_state->connection,
                                                  player_num);

                enum vsx_game_state_player_flag flags = 0;

                if (vsx_player_is_connected(player))
                        flags |= VSX_GAME_STATE_PLAYER_FLAG_CONNECTED;
                if (vsx_player_is_typing(player))
                        flags |= VSX_GAME_STATE_PLAYER_FLAG_TYPING;
                if (vsx_player_has_next_turn(player))
                        flags |= VSX_GAME_STATE_PLAYER_FLAG_NEXT_TURN;
                if (vsx_player_is_shouting(player))
                        flags |= VSX_GAME_STATE_PLAYER_FLAG_SHOUTING;

                game_state->players[player_num].flags = flags;

                game_state->dirty_player_flags &= ~(1 << player_num);
        }
}

static void
ensure_n_tiles(struct vsx_game_state *game_state,
               int n_tiles)
{
        size_t new_length = sizeof (struct vsx_game_state_tile *) * n_tiles;
        size_t old_length = game_state->tiles_by_index.length;

        if (new_length > old_length) {
                vsx_buffer_set_length(&game_state->tiles_by_index, new_length);
                memset(game_state->tiles_by_index.data + old_length,
                       0,
                       new_length - old_length);
        }
}

static struct vsx_game_state_tile *
get_tile_by_index(struct vsx_game_state *game_state,
                  int tile_num)
{
        ensure_n_tiles(game_state, tile_num + 1);

        struct vsx_game_state_tile **tile_pointers =
                (struct vsx_game_state_tile **) game_state->tiles_by_index.data;

        struct vsx_game_state_tile *tile = tile_pointers[tile_num];

        if (tile == NULL) {
                tile = vsx_slab_allocate(&game_state->tile_allocator,
                                         sizeof *tile,
                                         alignof *tile);
                tile_pointers[tile_num] = tile;
                vsx_list_insert(game_state->tile_list.prev, &tile->link);
        }

        return tile;
}

static void
update_tiles_locked(struct vsx_game_state *game_state)
{
        size_t n_elements =
                game_state->dirty_tiles.length / sizeof (vsx_bitmask_element_t);
        vsx_bitmask_element_t *elements =
                (vsx_bitmask_element_t *) game_state->dirty_tiles.data;

        for (unsigned i = 0; i < n_elements; i++) {
                int bit;

                while ((bit = ffsl(elements[i]))) {
                        int tile_num = (i * VSX_BITMASK_BITS_PER_ELEMENT +
                                        bit - 1);

                        const struct vsx_tile *tile =
                                vsx_connection_get_tile(game_state->connection,
                                                        tile_num);

                        struct vsx_game_state_tile *state_tile =
                                get_tile_by_index(game_state, tile_num);

                        state_tile->x = vsx_tile_get_x(tile);
                        state_tile->y = vsx_tile_get_y(tile);
                        state_tile->letter = vsx_tile_get_letter(tile);

                        /* Move the tile to the end of the list so
                         * that the list will always been in reverse
                         * order of most recently updated.
                         */
                        vsx_list_remove(&state_tile->link);
                        vsx_list_insert(game_state->tile_list.prev,
                                        &state_tile->link);

                        elements[i] &= ~(1UL << (bit - 1));
                }
        }
}

size_t
vsx_game_state_get_n_tiles(struct vsx_game_state *game_state)
{
        return (game_state->tiles_by_index.length /
                sizeof (struct vsx_game_state_tile *));
}

void
vsx_game_state_foreach_tile(struct vsx_game_state *game_state,
                            vsx_game_state_foreach_tile_cb cb,
                            void *user_data)
{
        struct vsx_game_state_tile *tile;

        vsx_list_for_each(tile, &game_state->tile_list, link) {
                cb(tile->x, tile->y, tile->letter, user_data);
        }
}

void
vsx_game_state_foreach_player(struct vsx_game_state *game_state,
                              vsx_game_state_foreach_player_cb cb,
                              void *user_data)
{
        for (int i = 0; i < VSX_GAME_STATE_N_VISIBLE_PLAYERS; i++) {
                struct vsx_game_state_player *player = game_state->players + i;

                cb(player->name, player->flags, user_data);
        }
}

void
vsx_game_state_update(struct vsx_game_state *game_state)
{
        vsx_worker_lock(game_state->worker);
        pthread_mutex_lock(&game_state->mutex);

        update_player_names_locked(game_state);
        update_player_flags_locked(game_state);
        update_tiles_locked(game_state);

        pthread_mutex_unlock(&game_state->mutex);
        vsx_worker_unlock(game_state->worker);
}

static void
handle_player_changed(struct vsx_game_state *game_state,
                      const struct vsx_connection_event *event)
{
        const struct vsx_player *player = event->player_changed.player;
        int player_num = vsx_player_get_number(player);

        if (player_num >= VSX_GAME_STATE_N_VISIBLE_PLAYERS)
                return;

        pthread_mutex_lock(&game_state->mutex);

        if ((event->player_changed.flags &
             VSX_CONNECTION_PLAYER_CHANGED_FLAGS_NAME)) {
                game_state->dirty_player_names |= 1 << player_num;
        }

        if ((event->player_changed.flags &
             (VSX_CONNECTION_PLAYER_CHANGED_FLAGS_FLAGS |
              VSX_CONNECTION_PLAYER_CHANGED_FLAGS_SHOUTING))) {
                game_state->dirty_player_flags |= 1 << player_num;
        }

        pthread_mutex_unlock(&game_state->mutex);
}

static void
handle_tile_changed(struct vsx_game_state *game_state,
                    const struct vsx_connection_event *event)
{
        const struct vsx_tile *tile = event->tile_changed.tile;
        int tile_num = vsx_tile_get_number(tile);

        pthread_mutex_lock(&game_state->mutex);

        vsx_bitmask_set_buffer(&game_state->dirty_tiles, tile_num, true);

        pthread_mutex_unlock(&game_state->mutex);
}

static void
event_cb(struct vsx_listener *listener,
         void *data)
{
        struct vsx_game_state *game_state =
                vsx_container_of(listener,
                                 struct vsx_game_state,
                                 event_listener);
        const struct vsx_connection_event *event = data;

        switch (event->type) {
        case VSX_CONNECTION_EVENT_TYPE_PLAYER_CHANGED:
                handle_player_changed(game_state, event);
                break;
        case VSX_CONNECTION_EVENT_TYPE_TILE_CHANGED:
                handle_tile_changed(game_state, event);
                break;
        default:
                break;
        }
}

void
vsx_game_state_shout(struct vsx_game_state *game_state)
{
        vsx_worker_lock(game_state->worker);
        vsx_connection_shout(game_state->connection);
        vsx_worker_unlock(game_state->worker);
}

void
vsx_game_state_turn(struct vsx_game_state *game_state)
{
        vsx_worker_lock(game_state->worker);
        vsx_connection_turn(game_state->connection);
        vsx_worker_unlock(game_state->worker);
}

struct vsx_game_state *
vsx_game_state_new(struct vsx_worker *worker,
                   struct vsx_connection *connection)
{
        struct vsx_game_state *game_state = vsx_calloc(sizeof *game_state);

        pthread_mutex_init(&game_state->mutex, NULL /* attr */);

        vsx_buffer_init(&game_state->dirty_tiles);
        vsx_buffer_init(&game_state->tiles_by_index);
        vsx_slab_init(&game_state->tile_allocator);
        vsx_list_init(&game_state->tile_list);

        game_state->worker = worker;
        game_state->connection = connection;

        vsx_worker_lock(game_state->worker);

        game_state->event_listener.notify = event_cb;
        vsx_signal_add(vsx_connection_get_event_signal(connection),
                       &game_state->event_listener);

        vsx_worker_unlock(game_state->worker);

        return game_state;
}

void
vsx_game_state_free(struct vsx_game_state *game_state)
{
        vsx_list_remove(&game_state->event_listener.link);

        for (int i = 0; i < VSX_GAME_STATE_N_VISIBLE_PLAYERS; i++)
                vsx_free(game_state->players[i].name);

        vsx_buffer_destroy(&game_state->dirty_tiles);
        vsx_buffer_destroy(&game_state->tiles_by_index);
        vsx_slab_destroy(&game_state->tile_allocator);

        pthread_mutex_destroy(&game_state->mutex);

        vsx_free(game_state);
}
