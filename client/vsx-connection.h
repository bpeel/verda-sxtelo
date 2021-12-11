/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2012, 2013, 2021  Neil Roberts
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

#ifndef VSX_CONNECTION_H
#define VSX_CONNECTION_H

#include <stdint.h>

#include "vsx-player.h"
#include "vsx-tile.h"
#include "vsx-signal.h"
#include "vsx-netaddress.h"
#include "vsx-error.h"

struct vsx_connection;

enum vsx_connection_state {
        VSX_CONNECTION_STATE_AWAITING_HEADER,
        VSX_CONNECTION_STATE_IN_PROGRESS,
        VSX_CONNECTION_STATE_DONE
};

enum vsx_connection_event_type {
        /* Emitted whenever the connection encounters an error. These
         * could be either an I/O error from the underlying socket or
         * a protocol error. Usually the connection will try to
         * recover from the error by reconnecting, but you can prevent
         * this in the signal handler by calling
         * vsx_connection_set_running().
         */
        VSX_CONNECTION_EVENT_TYPE_ERROR,
        VSX_CONNECTION_EVENT_TYPE_MESSAGE,
        /* Emitted whenever the details of a player have changed or a
         * new player has been created.
         */
        VSX_CONNECTION_EVENT_TYPE_PLAYER_CHANGED,
        VSX_CONNECTION_EVENT_TYPE_PLAYER_SHOUTED,
        VSX_CONNECTION_EVENT_TYPE_TILE_CHANGED,
        VSX_CONNECTION_EVENT_TYPE_RUNNING_STATE_CHANGED,
        VSX_CONNECTION_EVENT_TYPE_STATE_CHANGED,
        VSX_CONNECTION_EVENT_TYPE_POLL_CHANGED,
};

enum vsx_connection_player_changed_flags {
        VSX_CONNECTION_PLAYER_CHANGED_FLAGS_NAME = (1 << 0),
        VSX_CONNECTION_PLAYER_CHANGED_FLAGS_FLAGS = (1 << 1),
};

struct vsx_connection_event {
        enum vsx_connection_event_type type;

        union {
                struct {
                        struct vsx_error *error;
                } error;

                struct {
                        const struct vsx_player *player;
                        const char *message;
                } message;

                struct {
                        const struct vsx_player *player;
                        enum vsx_connection_player_changed_flags flags;
                } player_changed;

                struct {
                        const struct vsx_player *player;
                } player_shouted;

                struct {
                        bool new_tile;
                        const struct vsx_tile *tile;
                } tile_changed;

                struct {
                        bool running;
                } running_state_changed;

                struct {
                        enum vsx_connection_state state;
                } state_changed;

                struct {
                        /* The next monotonic time that the connection
                         * should be woken up at if nothing else wakes
                         * it up beforehand. Otherwise this will be
                         * INT64 if the connection doesn’t need to be
                         * woken up for a timeout.
                         */
                        int64_t wakeup_time;
                        /* A file descriptor to poll on, or -1 if no
                         * polling needs to be done.
                         */
                        int fd;
                        /* A set of flags to poll on */
                        short events;
                } poll_changed;
        };
};

enum vsx_connection_error {
        VSX_CONNECTION_ERROR_BAD_DATA,
        VSX_CONNECTION_ERROR_CONNECTION_CLOSED
};

extern struct vsx_error_domain
vsx_connection_error;

struct vsx_connection *
vsx_connection_new(const struct vsx_netaddress *address,
                   const char *room,
                   const char *player_name);

void
vsx_connection_wake_up(struct vsx_connection *connection,
                       short poll_events);

void
vsx_connection_set_running(struct vsx_connection *connection,
                           bool running);

bool
vsx_connection_get_running(struct vsx_connection *connection);

bool
vsx_connection_get_typing(struct vsx_connection *connection);

void
vsx_connection_set_typing(struct vsx_connection *connection,
                          bool typing);

void
vsx_connection_shout(struct vsx_connection *connection);

void
vsx_connection_turn(struct vsx_connection *connection);

void
vsx_connection_move_tile(struct vsx_connection *connection,
                         int tile_num,
                         int x, int y);

enum vsx_connection_state
vsx_connection_get_state(struct vsx_connection *connection);

void
vsx_connection_send_message(struct vsx_connection *connection,
                            const char *message);

void
vsx_connection_leave(struct vsx_connection *connection);

const struct vsx_player *
vsx_connection_get_player(struct vsx_connection *connection,
                          int player_num);

typedef void
(* vsx_connection_foreach_player_cb)(const struct vsx_player *player,
                                     void *user_data);

void
vsx_connection_foreach_player(struct vsx_connection *connection,
                              vsx_connection_foreach_player_cb callback,
                              void *user_data);

const struct vsx_player *
vsx_connection_get_self(struct vsx_connection *connection);

const struct vsx_tile *
vsx_connection_get_tile(struct vsx_connection *connection,
                        int tile_num);

typedef void
(* vsx_connection_foreach_tile_cb)(const struct vsx_tile *tile,
                                   void *user_data);

void
vsx_connection_foreach_tile(struct vsx_connection *connection,
                            vsx_connection_foreach_tile_cb callback,
                            void *user_data);

struct vsx_signal *
vsx_connection_get_event_signal(struct vsx_connection *connection);

void
vsx_connection_free(struct vsx_connection *connection);

#endif /* VSX_CONNECTION_H */
