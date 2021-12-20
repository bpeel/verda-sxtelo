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

#ifndef VSX_GAME_STATE_H
#define VSX_GAME_STATE_H

#include <stdlib.h>

#include "vsx-connection.h"

/* This maintains a copy of the game state reported by the
 * vsx_connection so that it can be painted. The state is only
 * accessed from the main thread so it doesn’t need a mutex. When the
 * connection reports that something has changed the game state will
 * set a dirty flag in order to update the state later on the main
 * thread.
 */

#define VSX_GAME_STATE_N_VISIBLE_PLAYERS 6

enum vsx_game_state_player_flag {
        VSX_GAME_STATE_PLAYER_FLAG_CONNECTED = (1 << 0),
        VSX_GAME_STATE_PLAYER_FLAG_TYPING = (1 << 1),
        VSX_GAME_STATE_PLAYER_FLAG_NEXT_TURN = (1 << 2),
};

struct vsx_game_state *
vsx_game_state_new(struct vsx_connection *connection);

size_t
vsx_game_state_get_n_tiles(struct vsx_game_state *game_state);

typedef void
(* vsx_game_state_foreach_tile_cb)(int x, int y,
                                   uint32_t letter,
                                   void *user_data);

void
vsx_game_state_foreach_tile(struct vsx_game_state *game_state,
                            vsx_game_state_foreach_tile_cb cb,
                            void *user_data);

void
vsx_game_state_update(struct vsx_game_state *game_state);

void
vsx_game_state_free(struct vsx_game_state *game_state);

#endif /* VSX_GAME_STATE_H */
