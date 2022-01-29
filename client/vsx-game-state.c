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
#include "vsx-main-thread.h"
#include "vsx-instance-state.h"

struct vsx_game_state_player {
        char *name;
        enum vsx_game_state_player_flag flags;
};

struct vsx_game_state_tile {
        struct vsx_connection_event event;
        struct vsx_list link;
};

struct queued_event {
        struct vsx_list link;
        struct vsx_connection_event event;
};

struct vsx_game_state {
        /* This data is only accessed from the main thread and doesn’t
         * need a mutex.
         */

        struct vsx_game_state_player players[VSX_GAME_STATE_N_VISIBLE_PLAYERS];

        int shouting_player;
        struct vsx_main_thread_token *remove_shout_timeout;

        bool has_conversation_id;
        uint64_t conversation_id;

        enum vsx_dialog dialog;

        int self;

        /* Array of tile pointers indexed by tile number */
        struct vsx_buffer tiles_by_index;
        /* List of tiles in reverse order of last updated */
        struct vsx_list tile_list;
        /* Slab allocator for the tiles */
        struct vsx_slab_allocator tile_allocator;

        /* The number of tiles in the game, as reported by the server.
         * This includes the tiles that are still in the bag so it is
         * just used to determine the length of the game.
         */
        int n_tiles;

        /* The language reported by the server */
        enum vsx_text_language language;

        int name_y_pos;
        int name_width, name_height;

        enum vsx_text name_note;

        struct vsx_main_thread *main_thread;
        struct vsx_worker *worker;
        struct vsx_connection *connection;
        struct vsx_listener event_listener;

        struct vsx_signal event_signal;
        struct vsx_signal modified_signal;

        struct vsx_main_thread_token *reset_on_idle_token;

        pthread_mutex_t mutex;

        /* The following data is protected by the mutex and can be
         * modified from any thread.
         */

        struct vsx_list event_queue;
        struct vsx_main_thread_token *flush_queue_token;

        struct vsx_list freed_events;

        /* The instance state is also protected by the mutex so that
         * it can be accessed from the android UI main thread (ie, not
         * the render thread)
         */
        struct vsx_instance_state instance_state;
};

/* When a shout event is received, the player will remain shouting
 * until this number of microseconds passes.
 */
#define VSX_GAME_STATE_SHOUT_TIME (10 * 1000 * 1000)

static void
ensure_n_tiles(struct vsx_game_state *game_state,
               int n_tiles)
{
        size_t new_length =
                sizeof (struct vsx_game_state_tile *) * n_tiles;
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
                (struct vsx_game_state_tile **)
                game_state->tiles_by_index.data;

        struct vsx_game_state_tile *tile = tile_pointers[tile_num];

        if (tile == NULL) {
                tile = vsx_slab_allocate(&game_state->tile_allocator,
                                         sizeof *tile,
                                         alignof (struct vsx_game_state_tile));
                tile_pointers[tile_num] = tile;
                vsx_list_insert(game_state->tile_list.prev, &tile->link);
        }

        return tile;
}

int
vsx_game_state_get_n_tiles(struct vsx_game_state *game_state)
{
        return game_state->n_tiles;
}

int
vsx_game_state_get_remaining_tiles(struct vsx_game_state *game_state)
{
        return (game_state->n_tiles -
                game_state->tiles_by_index.length /
                sizeof (struct vsx_game_state_tile *));
}

enum vsx_text_language
vsx_game_state_get_language(struct vsx_game_state *game_state)
{
        return game_state->language;
}

void
vsx_game_state_foreach_tile(struct vsx_game_state *game_state,
                            vsx_game_state_foreach_tile_cb cb,
                            void *user_data)
{
        struct vsx_game_state_tile *tile;

        vsx_list_for_each(tile, &game_state->tile_list, link) {
                cb(&tile->event, user_data);
        }
}

void
vsx_game_state_foreach_player(struct vsx_game_state *game_state,
                              vsx_game_state_foreach_player_cb cb,
                              void *user_data)
{
        for (int i = 0; i < VSX_GAME_STATE_N_VISIBLE_PLAYERS; i++) {
                struct vsx_game_state_player *player = game_state->players + i;

                cb(i, player->name, player->flags, user_data);
        }
}

static void
clear_remove_shout_timeout(struct vsx_game_state *game_state)
{
        if (game_state->remove_shout_timeout == NULL)
                return;

        vsx_main_thread_cancel_idle(game_state->remove_shout_timeout);

        game_state->remove_shout_timeout = NULL;
}

static void
remove_shout(struct vsx_game_state *game_state)
{
        if (game_state->shouting_player == -1)
                return;

        clear_remove_shout_timeout(game_state);

        game_state->shouting_player = -1;

        struct vsx_game_state_modified_event event = {
                .type = VSX_GAME_STATE_MODIFIED_TYPE_SHOUTING_PLAYER,
        };

        vsx_signal_emit(&game_state->modified_signal, &event);
}

static void
remove_shout_cb(void *data)
{
        struct vsx_game_state *game_state = data;

        game_state->remove_shout_timeout = NULL;

        remove_shout(game_state);
}

static void
remove_conversation_id(struct vsx_game_state *game_state)
{
        if (!game_state->has_conversation_id)
                return;

        game_state->has_conversation_id = false;

        struct vsx_game_state_modified_event event = {
                .type = VSX_GAME_STATE_MODIFIED_TYPE_CONVERSATION_ID,
        };

        vsx_signal_emit(&game_state->modified_signal, &event);
}

static void
reset_on_idle_cb(void *user_data)
{
        struct vsx_game_state *game_state = user_data;

        game_state->reset_on_idle_token = NULL;

        vsx_game_state_reset(game_state);
}

static void
queue_reset_on_idle(struct vsx_game_state *game_state)
{
        if (game_state->reset_on_idle_token)
                return;

        game_state->reset_on_idle_token =
                vsx_main_thread_queue_idle(game_state->main_thread,
                                           reset_on_idle_cb,
                                           game_state);
}

static enum vsx_text_language
get_language_for_code(const char *code)
{
        for (int i = 0; i < VSX_TEXT_N_LANGUAGES; i++) {
                if (!strcmp(code, vsx_text_get(i, VSX_TEXT_LANGUAGE_CODE)))
                        return i;
        }

        return VSX_TEXT_LANGUAGE_ENGLISH;
}

static void
handle_header(struct vsx_game_state *game_state,
              const struct vsx_connection_event *event)
{
        game_state->self = event->header.self_num;
}

static void
handle_conversation_id(struct vsx_game_state *game_state,
                       const struct vsx_connection_event *event)
{
        if (game_state->has_conversation_id &&
            game_state->conversation_id == event->conversation_id.id)
                return;

        game_state->has_conversation_id = true;
        game_state->conversation_id = event->conversation_id.id;

        struct vsx_game_state_modified_event m_event = {
                .type = VSX_GAME_STATE_MODIFIED_TYPE_CONVERSATION_ID,
        };

        vsx_signal_emit(&game_state->modified_signal, &m_event);
}

static void
handle_player_name_changed(struct vsx_game_state *game_state,
                           const struct vsx_connection_event *event)
{
        int player_num = event->player_name_changed.player_num;

        if (player_num >= VSX_GAME_STATE_N_VISIBLE_PLAYERS)
                return;

        struct vsx_game_state_player *player =
                game_state->players + player_num;

        vsx_free(player->name);
        player->name = vsx_strdup(event->player_name_changed.name);

        struct vsx_game_state_modified_event m_event = {
                .type = VSX_GAME_STATE_MODIFIED_TYPE_PLAYER_NAME,
                .player_name = {
                        .player_num = player_num,
                        .name = player->name,
                },
        };

        vsx_signal_emit(&game_state->modified_signal, &m_event);
}

static void
handle_player_flags_changed(struct vsx_game_state *game_state,
                            const struct vsx_connection_event *event)
{
        int player_num = event->player_flags_changed.player_num;

        if (player_num >= VSX_GAME_STATE_N_VISIBLE_PLAYERS)
                return;

        struct vsx_game_state_player *player =
                game_state->players + player_num;

        /* Leave the shouting flag as it was */
        enum vsx_game_state_player_flag new_flags =
                event->player_flags_changed.flags;

        if (new_flags == player->flags)
                return;

        player->flags = new_flags;

        struct vsx_game_state_modified_event m_event = {
                .type = VSX_GAME_STATE_MODIFIED_TYPE_PLAYER_FLAGS,
        };

        vsx_signal_emit(&game_state->modified_signal, &m_event);
}

static void
handle_player_shouted(struct vsx_game_state *game_state,
                      const struct vsx_connection_event *event)
{
        int player_num = event->player_shouted.player_num;

        clear_remove_shout_timeout(game_state);
        game_state->remove_shout_timeout =
                vsx_main_thread_queue_timeout(game_state->main_thread,
                                              VSX_GAME_STATE_SHOUT_TIME,
                                              remove_shout_cb,
                                              game_state);

        if (player_num == game_state->shouting_player)
                return;

        game_state->shouting_player = player_num;

        struct vsx_game_state_modified_event m_event = {
                .type = VSX_GAME_STATE_MODIFIED_TYPE_SHOUTING_PLAYER,
        };

        vsx_signal_emit(&game_state->modified_signal, &m_event);
}

static void
handle_tile_changed(struct vsx_game_state *game_state,
                    const struct vsx_connection_event *event)
{
        size_t old_length = game_state->tiles_by_index.length;

        struct vsx_game_state_tile *tile =
                get_tile_by_index(game_state, event->tile_changed.num);

        tile->event = *event;
        tile->event.synced = false;

        /* Move the tile to the end of the list so that the list will
         * always been in reverse order of most recently updated.
         */
        vsx_list_remove(&tile->link);
        vsx_list_insert(game_state->tile_list.prev, &tile->link);

        /* If this is a new tile then the number of tiles remaining
         * will have changed.
         */
        if (old_length < game_state->tiles_by_index.length) {
                struct vsx_game_state_modified_event m_event = {
                        .type = VSX_GAME_STATE_MODIFIED_TYPE_REMAINING_TILES,
                };

                vsx_signal_emit(&game_state->modified_signal, &m_event);
        }
}

static void
handle_n_tiles_changed(struct vsx_game_state *game_state,
                       const struct vsx_connection_event *event)
{
        if (game_state->n_tiles == event->n_tiles_changed.n_tiles)
                return;

        game_state->n_tiles = event->n_tiles_changed.n_tiles;

        struct vsx_game_state_modified_event m_event = {
                .type = VSX_GAME_STATE_MODIFIED_TYPE_N_TILES,
        };

        vsx_signal_emit(&game_state->modified_signal, &m_event);

        m_event.type = VSX_GAME_STATE_MODIFIED_TYPE_REMAINING_TILES;

        vsx_signal_emit(&game_state->modified_signal, &m_event);
}

static void
handle_language_changed(struct vsx_game_state *game_state,
                        const struct vsx_connection_event *event)
{
        enum vsx_text_language language =
                get_language_for_code(event->language_changed.code);

        if (language == game_state->language)
                return;

        game_state->language = language;

        struct vsx_game_state_modified_event m_event = {
                .type = VSX_GAME_STATE_MODIFIED_TYPE_LANGUAGE,
        };

        vsx_signal_emit(&game_state->modified_signal, &m_event);
}

static void
handle_bad_game(struct vsx_game_state *game_state)
{
        queue_reset_on_idle(game_state);

        const char *text = vsx_text_get(game_state->language,
                                        VSX_TEXT_BAD_GAME);
        vsx_game_state_set_note(game_state, text);
}

static void
handle_error(struct vsx_game_state *game_state,
             const struct vsx_connection_event *event)
{
        if (event->error.error->domain != &vsx_connection_error)
                return;

        switch (event->error.error->code) {
        case VSX_CONNECTION_ERROR_BAD_PLAYER_ID:
        case VSX_CONNECTION_ERROR_BAD_CONVERSATION_ID:
                handle_bad_game(game_state);
                break;
        default:
                break;
        }
}

static void
handle_end(struct vsx_game_state *game_state,
           const struct vsx_connection_event *event)
{
        /* This should probably only happen because the player has
         * clicked the leave button and the server has received the
         * request. Let’s just make the game reset without displaying
         * an error.
         */
        queue_reset_on_idle(game_state);
}

static void
handle_event(struct vsx_game_state *game_state,
             const struct vsx_connection_event *event)
{
        switch (event->type) {
        case VSX_CONNECTION_EVENT_TYPE_HEADER:
                handle_header(game_state, event);
                break;
        case VSX_CONNECTION_EVENT_TYPE_CONVERSATION_ID:
                handle_conversation_id(game_state, event);
                break;
        case VSX_CONNECTION_EVENT_TYPE_PLAYER_NAME_CHANGED:
                handle_player_name_changed(game_state, event);
                break;
        case VSX_CONNECTION_EVENT_TYPE_PLAYER_FLAGS_CHANGED:
                handle_player_flags_changed(game_state, event);
                break;
        case VSX_CONNECTION_EVENT_TYPE_PLAYER_SHOUTED:
                handle_player_shouted(game_state, event);
                break;
        case VSX_CONNECTION_EVENT_TYPE_TILE_CHANGED:
                handle_tile_changed(game_state, event);
                break;
        case VSX_CONNECTION_EVENT_TYPE_N_TILES_CHANGED:
                handle_n_tiles_changed(game_state, event);
                break;
        case VSX_CONNECTION_EVENT_TYPE_LANGUAGE_CHANGED:
                handle_language_changed(game_state, event);
                break;
        case VSX_CONNECTION_EVENT_TYPE_ERROR:
                handle_error(game_state, event);
                break;
        case VSX_CONNECTION_EVENT_TYPE_END:
                handle_end(game_state, event);
                break;
        default:
                break;
        }
}

static void
flush_queue_cb(void *data)
{
        struct vsx_game_state *game_state = data;

        pthread_mutex_lock(&game_state->mutex);

        game_state->flush_queue_token = NULL;

        struct vsx_list event_queue;
        vsx_list_init(&event_queue);
        vsx_list_insert_list(&event_queue, &game_state->event_queue);
        vsx_list_init(&game_state->event_queue);

        pthread_mutex_unlock(&game_state->mutex);

        struct queued_event *queued_event;

        vsx_list_for_each(queued_event, &event_queue, link) {
                handle_event(game_state, &queued_event->event);
                vsx_signal_emit(&game_state->event_signal,
                                &queued_event->event);
                vsx_connection_destroy_event(&queued_event->event);
        }

        pthread_mutex_lock(&game_state->mutex);

        vsx_list_insert_list(&game_state->freed_events, &event_queue);

        pthread_mutex_unlock(&game_state->mutex);
}

static void
handle_instance_state_event_locked(struct vsx_instance_state *instance_state,
                                   const struct vsx_connection_event *event)
{
        switch (event->type) {
        case VSX_CONNECTION_EVENT_TYPE_HEADER:
                instance_state->has_person_id = true;
                instance_state->person_id = event->header.person_id;
                break;

        default:
                break;
        }
}

static void
event_cb(struct vsx_listener *listener,
         void *data)
{
        const struct vsx_connection_event *event = data;

        /* Ignore poll_changed events because they will be frequent
         * and only interesting for the vsx_worker.
         */
        if (event->type == VSX_CONNECTION_EVENT_TYPE_POLL_CHANGED)
                return;

        struct vsx_game_state *game_state =
                vsx_container_of(listener,
                                 struct vsx_game_state,
                                 event_listener);

        pthread_mutex_lock(&game_state->mutex);

        struct queued_event *queued_event;

        if (vsx_list_empty(&game_state->freed_events)) {
                queued_event = vsx_alloc(sizeof *queued_event);
        } else {
                queued_event = vsx_container_of(game_state->freed_events.next,
                                                struct queued_event,
                                                link);
                vsx_list_remove(&queued_event->link);
        }

        vsx_connection_copy_event(&queued_event->event, event);

        vsx_list_insert(game_state->event_queue.prev, &queued_event->link);

        if (game_state->flush_queue_token == NULL) {
                game_state->flush_queue_token =
                        vsx_main_thread_queue_idle(game_state->main_thread,
                                                   flush_queue_cb,
                                                   game_state);
        }

        /* Handle instance state events here while the mutex is locked
         * instead of in the idle callback.
         */
        handle_instance_state_event_locked(&game_state->instance_state, event);

        pthread_mutex_unlock(&game_state->mutex);
}

static void
remove_reset_on_idle(struct vsx_game_state *game_state)
{
        if (game_state->reset_on_idle_token == NULL)
                return;

        vsx_main_thread_cancel_idle(game_state->reset_on_idle_token);
        game_state->reset_on_idle_token = NULL;
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

void
vsx_game_state_set_n_tiles(struct vsx_game_state *game_state,
                           int n_tiles)
{
        vsx_worker_lock(game_state->worker);
        vsx_connection_set_n_tiles(game_state->connection, n_tiles);
        vsx_worker_unlock(game_state->worker);
}

void
vsx_game_state_set_language(struct vsx_game_state *game_state,
                            const char *language_code)
{
        vsx_worker_lock(game_state->worker);
        vsx_connection_set_language(game_state->connection, language_code);
        vsx_worker_unlock(game_state->worker);
}

void
vsx_game_state_set_player_name(struct vsx_game_state *game_state,
                               const char *name)
{
        vsx_worker_lock(game_state->worker);
        vsx_connection_set_player_name(game_state->connection, name);
        vsx_worker_unlock(game_state->worker);
}

void
vsx_game_state_set_note(struct vsx_game_state *game_state,
                        const char *text)
{
        struct vsx_game_state_modified_event event = {
                .type = VSX_GAME_STATE_MODIFIED_TYPE_NOTE,

                .note = {
                        .text = text,
                },
        };

        vsx_signal_emit(&game_state->modified_signal, &event);
}


void
vsx_game_state_set_name_position(struct vsx_game_state *game_state,
                                 int y_pos,
                                 int width)
{
        if (game_state->name_y_pos == y_pos &&
            game_state->name_width == width)
                return;

        game_state->name_y_pos = y_pos;
        game_state->name_width = width;

        struct vsx_game_state_modified_event event = {
                .type = VSX_GAME_STATE_MODIFIED_TYPE_NAME_POSITION,
        };

        vsx_signal_emit(&game_state->modified_signal, &event);
}

void
vsx_game_state_get_name_position(struct vsx_game_state *game_state,
                                 int *y_pos,
                                 int *width)
{
        *y_pos = game_state->name_y_pos;
        *width = game_state->name_width;
}

void
vsx_game_state_set_name_height(struct vsx_game_state *game_state,
                               int height)
{
        if (game_state->name_height == height)
                return;

        game_state->name_height = height;

        struct vsx_game_state_modified_event event = {
                .type = VSX_GAME_STATE_MODIFIED_TYPE_NAME_HEIGHT,
        };

        vsx_signal_emit(&game_state->modified_signal, &event);
}

int
vsx_game_state_get_name_height(struct vsx_game_state *game_state)
{
        return game_state->name_height;
}

void
vsx_game_state_set_name_note(struct vsx_game_state *game_state,
                             enum vsx_text note)
{
        if (game_state->name_note == note)
                return;

        game_state->name_note = note;

        struct vsx_game_state_modified_event event = {
                .type = VSX_GAME_STATE_MODIFIED_TYPE_NAME_NOTE,
        };

        vsx_signal_emit(&game_state->modified_signal, &event);
}

enum vsx_text
vsx_game_state_get_name_note(struct vsx_game_state *game_state)
{
        return game_state->name_note;
}

bool
vsx_game_state_get_started(struct vsx_game_state *game_state)
{
        return game_state->tiles_by_index.length > 0;
}

void
vsx_game_state_move_tile(struct vsx_game_state *game_state,
                         int tile_num,
                         int x, int y)
{
        vsx_worker_lock(game_state->worker);
        vsx_connection_move_tile(game_state->connection,
                                 tile_num,
                                 x, y);
        vsx_worker_unlock(game_state->worker);
}

void
vsx_game_state_leave(struct vsx_game_state *game_state)
{
        vsx_worker_lock(game_state->worker);
        vsx_connection_leave(game_state->connection);
        vsx_worker_unlock(game_state->worker);
}

struct vsx_game_state *
vsx_game_state_new(struct vsx_main_thread *main_thread,
                   struct vsx_worker *worker,
                   struct vsx_connection *connection,
                   const char *default_language)
{
        struct vsx_game_state *game_state = vsx_calloc(sizeof *game_state);

        pthread_mutex_init(&game_state->mutex, NULL /* attr */);

        vsx_signal_init(&game_state->event_signal);
        vsx_signal_init(&game_state->modified_signal);

        vsx_list_init(&game_state->event_queue);
        vsx_list_init(&game_state->freed_events);

        vsx_buffer_init(&game_state->tiles_by_index);
        vsx_slab_init(&game_state->tile_allocator);
        vsx_list_init(&game_state->tile_list);

        game_state->shouting_player = -1;

        game_state->dialog = VSX_DIALOG_NAME;

        game_state->language = get_language_for_code(default_language);

        game_state->main_thread = main_thread;
        game_state->worker = worker;
        game_state->connection = connection;

        vsx_instance_state_init(&game_state->instance_state);

        game_state->instance_state.dialog = game_state->dialog;

        game_state->name_height = 30;
        game_state->name_note = VSX_TEXT_ENTER_NAME_NEW_GAME;

        vsx_worker_lock(game_state->worker);

        game_state->event_listener.notify = event_cb;
        vsx_signal_add(vsx_connection_get_event_signal(connection),
                       &game_state->event_listener);

        vsx_worker_unlock(game_state->worker);

        return game_state;
}

static void
free_event_queue(struct vsx_game_state *game_state)
{
        struct queued_event *queued_event;

        vsx_list_for_each(queued_event,
                          &game_state->event_queue,
                          link) {
                vsx_connection_destroy_event(&queued_event->event);
        }

        vsx_list_insert_list(&game_state->freed_events,
                             &game_state->event_queue);
        vsx_list_init(&game_state->event_queue);
}

static void
reset_player_names(struct vsx_game_state *game_state)
{
        for (unsigned i = 0; i < VSX_N_ELEMENTS(game_state->players); i++) {
                struct vsx_game_state_player *player = game_state->players + i;

                if (player->name == NULL || *player->name == '\0')
                        continue;

                vsx_free(player->name);
                player->name = vsx_strdup("");

                struct vsx_game_state_modified_event event = {
                        .type = VSX_GAME_STATE_MODIFIED_TYPE_PLAYER_NAME,
                        .player_name = {
                                .player_num = i,
                                .name = player->name,
                        },
                };

                vsx_signal_emit(&game_state->modified_signal, &event);
        }
}

static void
reset_player_flags(struct vsx_game_state *game_state)
{
        bool changed = false;

        for (unsigned i = 0; i < VSX_N_ELEMENTS(game_state->players); i++) {
                struct vsx_game_state_player *player = game_state->players + i;

                if (player->flags == 0)
                        continue;

                player->flags = 0;
                changed = true;
        }

        if (changed) {
                struct vsx_game_state_modified_event event = {
                        .type = VSX_GAME_STATE_MODIFIED_TYPE_PLAYER_FLAGS,
                };

                vsx_signal_emit(&game_state->modified_signal, &event);
        }
}

static void
reset_tiles(struct vsx_game_state *game_state)
{
        bool had_tiles = game_state->tiles_by_index.length > 0;

        vsx_buffer_set_length(&game_state->tiles_by_index, 0);
        vsx_list_init(&game_state->tile_list);
        vsx_slab_destroy(&game_state->tile_allocator);
        vsx_slab_init(&game_state->tile_allocator);

        if (had_tiles) {
                struct vsx_game_state_modified_event event = {
                        .type = VSX_GAME_STATE_MODIFIED_TYPE_REMAINING_TILES,
                };

                vsx_signal_emit(&game_state->modified_signal, &event);
        }
}

void
vsx_game_state_reset(struct vsx_game_state *game_state)
{
        vsx_worker_lock(game_state->worker);
        vsx_connection_reset(game_state->connection);
        vsx_connection_set_language(game_state->connection,
                                    vsx_text_get(game_state->language,
                                                 VSX_TEXT_LANGUAGE_CODE));
        vsx_connection_set_running(game_state->connection, true);
        vsx_worker_unlock(game_state->worker);

        pthread_mutex_lock(&game_state->mutex);

        free_event_queue(game_state);

        if (game_state->flush_queue_token) {
                vsx_main_thread_cancel_idle(game_state->flush_queue_token);
                game_state->flush_queue_token = NULL;
        }

        game_state->instance_state.has_person_id = false;

        pthread_mutex_unlock(&game_state->mutex);

        remove_reset_on_idle(game_state);

        remove_shout(game_state);
        remove_conversation_id(game_state);
        reset_player_names(game_state);
        reset_player_flags(game_state);
        vsx_game_state_set_dialog(game_state, VSX_DIALOG_NAME);
        vsx_game_state_set_name_note(game_state, VSX_TEXT_ENTER_NAME_NEW_GAME);

        reset_tiles(game_state);

        struct vsx_game_state_modified_event event = {
                .type = VSX_GAME_STATE_MODIFIED_TYPE_RESET,
        };

        vsx_signal_emit(&game_state->modified_signal, &event);
}

int
vsx_game_state_get_shouting_player(struct vsx_game_state *game_state)
{
        return game_state->shouting_player;
}

bool
vsx_game_state_get_conversation_id(struct vsx_game_state *game_state,
                                   uint64_t *id)
{
        if (game_state->has_conversation_id) {
                *id = game_state->conversation_id;
                return true;
        } else {
                return false;
        }
}

enum vsx_dialog
vsx_game_state_get_dialog(struct vsx_game_state *game_state)
{
        return game_state->dialog;
}

void
vsx_game_state_set_dialog(struct vsx_game_state *game_state,
                          enum vsx_dialog dialog)
{
        if (game_state->dialog == dialog)
                return;

        game_state->dialog = dialog;

        pthread_mutex_lock(&game_state->mutex);
        game_state->instance_state.dialog = dialog;
        pthread_mutex_unlock(&game_state->mutex);

        struct vsx_game_state_modified_event event = {
                .type = VSX_GAME_STATE_MODIFIED_TYPE_DIALOG,
        };

        vsx_signal_emit(&game_state->modified_signal, &event);
}

int
vsx_game_state_get_self(struct vsx_game_state *game_state)
{
        return game_state->self;
}

char *
vsx_game_state_save_instance_state(struct vsx_game_state *game_state)
{
        char *str;

        pthread_mutex_lock(&game_state->mutex);

        str = vsx_instance_state_save(&game_state->instance_state);

        pthread_mutex_unlock(&game_state->mutex);

        return str;
}

void
vsx_game_state_load_instance_state(struct vsx_game_state *game_state,
                                   const char *str)
{
        bool has_person_id;
        uint64_t person_id;
        enum vsx_dialog dialog;

        pthread_mutex_lock(&game_state->mutex);

        vsx_instance_state_load(&game_state->instance_state, str);
        has_person_id = game_state->instance_state.has_person_id;
        person_id = game_state->instance_state.person_id;
        dialog = game_state->instance_state.dialog;

        pthread_mutex_unlock(&game_state->mutex);

        if (has_person_id) {
                vsx_worker_lock(game_state->worker);
                vsx_connection_set_person_id(game_state->connection,
                                             person_id);
                vsx_worker_unlock(game_state->worker);
        }

        vsx_game_state_set_dialog(game_state, dialog);
}

struct vsx_signal *
vsx_game_state_get_event_signal(struct vsx_game_state *game_state)
{
        return &game_state->event_signal;
}

struct vsx_signal *
vsx_game_state_get_modified_signal(struct vsx_game_state *game_state)
{
        return &game_state->modified_signal;
}

static void
free_freed_events(struct vsx_game_state *game_state)
{
        struct queued_event *queued_event, *tmp;

        vsx_list_for_each_safe(queued_event,
                               tmp,
                               &game_state->freed_events,
                               link) {
                vsx_free(queued_event);
        }
}

void
vsx_game_state_free(struct vsx_game_state *game_state)
{
        vsx_worker_lock(game_state->worker);
        vsx_list_remove(&game_state->event_listener.link);
        vsx_worker_unlock(game_state->worker);

        for (int i = 0; i < VSX_GAME_STATE_N_VISIBLE_PLAYERS; i++)
                vsx_free(game_state->players[i].name);

        if (game_state->flush_queue_token)
                vsx_main_thread_cancel_idle(game_state->flush_queue_token);

        remove_reset_on_idle(game_state);

        clear_remove_shout_timeout(game_state);

        free_event_queue(game_state);
        free_freed_events(game_state);

        vsx_buffer_destroy(&game_state->tiles_by_index);
        vsx_slab_destroy(&game_state->tile_allocator);

        pthread_mutex_destroy(&game_state->mutex);

        vsx_free(game_state);
}
