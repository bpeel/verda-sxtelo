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
#include <stdbool.h>

#include "vsx-main-thread.h"
#include "vsx-connection.h"
#include "vsx-worker.h"
#include "vsx-signal.h"
#include "vsx-dialog.h"
#include "vsx-text.h"

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

enum vsx_game_state_modified_type {
        VSX_GAME_STATE_MODIFIED_TYPE_PLAYER_FLAGS,
        VSX_GAME_STATE_MODIFIED_TYPE_PLAYER_NAME,
        VSX_GAME_STATE_MODIFIED_TYPE_SHOUTING_PLAYER,
        VSX_GAME_STATE_MODIFIED_TYPE_CONVERSATION_ID,
        VSX_GAME_STATE_MODIFIED_TYPE_DIALOG,
        VSX_GAME_STATE_MODIFIED_TYPE_N_TILES,
        VSX_GAME_STATE_MODIFIED_TYPE_LANGUAGE,
        VSX_GAME_STATE_MODIFIED_TYPE_REMAINING_TILES,
        VSX_GAME_STATE_MODIFIED_TYPE_NOTE,
        VSX_GAME_STATE_MODIFIED_TYPE_NAME_POSITION,
        VSX_GAME_STATE_MODIFIED_TYPE_NAME_HEIGHT,
        VSX_GAME_STATE_MODIFIED_TYPE_NAME_NOTE,
        VSX_GAME_STATE_MODIFIED_TYPE_RESET,
};

struct vsx_game_state_modified_event {
        enum vsx_game_state_modified_type type;

        union {
                struct {
                        int player_num;
                        const char *name;
                } player_name;

                struct {
                        const char *text;
                } note;
        };
};

struct vsx_game_state *
vsx_game_state_new(struct vsx_main_thread *main_thread,
                   struct vsx_worker *worker,
                   struct vsx_connection *connection,
                   const char *default_language);

/* Resets the connection and all the stored state to be the same as a
 * new game state except that the last language reported by the
 * connection will be preserved. The connection will be set as running
 * but it won’t actually connect until a name is set on it. Any
 * appropriate events for the state changes will be sent as well as a
 * final RESET event.
 */
void
vsx_game_state_reset(struct vsx_game_state *game_state);

/* Gets the number of tiles in the game, as reported by the server.
 * This includes the tiles that are still in the bag so it is just
 * used to determine the length of the game.
 */
int
vsx_game_state_get_n_tiles(struct vsx_game_state *game_state);

enum vsx_text_language
vsx_game_state_get_language(struct vsx_game_state *game_state);

typedef void
(* vsx_game_state_foreach_tile_cb)(const struct vsx_connection_event *tile,
                                   void *user_data);

void
vsx_game_state_foreach_tile(struct vsx_game_state *game_state,
                            vsx_game_state_foreach_tile_cb cb,
                            void *user_data);

typedef void
(* vsx_game_state_foreach_player_cb)(int player_num,
                                     const char *name,
                                     enum vsx_game_state_player_flag flags,
                                     void *user_data);

void
vsx_game_state_foreach_player(struct vsx_game_state *game_state,
                              vsx_game_state_foreach_player_cb,
                              void *user_data);

int
vsx_game_state_get_self(struct vsx_game_state *game_state);

int
vsx_game_state_get_shouting_player(struct vsx_game_state *game_state);

bool
vsx_game_state_get_conversation_id(struct vsx_game_state *game_state,
                                   uint64_t *id);

enum vsx_dialog
vsx_game_state_get_dialog(struct vsx_game_state *game_state);

void
vsx_game_state_set_dialog(struct vsx_game_state *game_state,
                          enum vsx_dialog dialog);

void
vsx_game_state_update(struct vsx_game_state *game_state);

void
vsx_game_state_shout(struct vsx_game_state *game_state);

void
vsx_game_state_turn(struct vsx_game_state *game_state);

void
vsx_game_state_set_n_tiles(struct vsx_game_state *game_state,
                           int n_tiles);

int
vsx_game_state_get_remaining_tiles(struct vsx_game_state *game_state);

void
vsx_game_state_set_language(struct vsx_game_state *game_state,
                            const char *language_code);

void
vsx_game_state_set_player_name(struct vsx_game_state *game_state,
                               const char *player_name);

void
vsx_game_state_set_note(struct vsx_game_state *game_state,
                        const char *text);

void
vsx_game_state_set_name_position(struct vsx_game_state *game_state,
                                 int y_pos,
                                 int width);

void
vsx_game_state_get_name_position(struct vsx_game_state *game_state,
                                 int *y_pos,
                                 int *width);

void
vsx_game_state_set_name_height(struct vsx_game_state *game_state,
                               int height);

int
vsx_game_state_get_name_height(struct vsx_game_state *game_state);

void
vsx_game_state_set_name_note(struct vsx_game_state *game_state,
                             enum vsx_text note);

enum vsx_text
vsx_game_state_get_name_note(struct vsx_game_state *game_state);

bool
vsx_game_state_get_started(struct vsx_game_state *game_state);

void
vsx_game_state_move_tile(struct vsx_game_state *game_state,
                         int tile_num,
                         int x, int y);

/* This can be called from any thread. The caller must free the
 * returned string.
 */
char *
vsx_game_state_save_instance_state(struct vsx_game_state *game_state);

/* This must be called from the main thread */
void
vsx_game_state_load_instance_state(struct vsx_game_state *game_state,
                                   const char *str);

/* These signals will only ever be emitted from the main thread */
struct vsx_signal *
vsx_game_state_get_event_signal(struct vsx_game_state *game_state);

struct vsx_signal *
vsx_game_state_get_modified_signal(struct vsx_game_state *game_state);

void
vsx_game_state_free(struct vsx_game_state *game_state);

#endif /* VSX_GAME_STATE_H */
