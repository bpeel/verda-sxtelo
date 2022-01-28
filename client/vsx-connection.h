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
#include <stdbool.h>

#include "vsx-signal.h"
#include "vsx-netaddress.h"
#include "vsx-error.h"

struct vsx_connection;

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
        VSX_CONNECTION_EVENT_TYPE_HEADER,
        VSX_CONNECTION_EVENT_TYPE_CONVERSATION_ID,
        /* Emitted whenever the details of a player have changed or a
         * new player has been created.
         */
        VSX_CONNECTION_EVENT_TYPE_PLAYER_NAME_CHANGED,
        VSX_CONNECTION_EVENT_TYPE_PLAYER_FLAGS_CHANGED,
        VSX_CONNECTION_EVENT_TYPE_PLAYER_SHOUTED,
        VSX_CONNECTION_EVENT_TYPE_TILE_CHANGED,
        VSX_CONNECTION_EVENT_TYPE_N_TILES_CHANGED,
        VSX_CONNECTION_EVENT_TYPE_LANGUAGE_CHANGED,
        VSX_CONNECTION_EVENT_TYPE_RUNNING_STATE_CHANGED,
        VSX_CONNECTION_EVENT_TYPE_END,
        VSX_CONNECTION_EVENT_TYPE_POLL_CHANGED,
};

struct vsx_connection_event {
        enum vsx_connection_event_type type;

        /* True if this as a new event triggered from a normal event
         * that has recently occured, or false if this event is
         * triggered because the connection is catching up to the
         * server state such as after first connecting or
         * reconnecting. This can be used for example to decide
         * whether to animate the event.
         */
        bool synced;

        union {
                struct {
                        struct vsx_error *error;
                } error;

                struct {
                        uint8_t self_num;
                        uint64_t person_id;
                } header;

                struct {
                        uint64_t id;
                } conversation_id;

                struct {
                        uint8_t player_num;
                        const char *message;
                } message;

                struct {
                        uint8_t player_num;
                        const char *name;
                } player_name_changed;

                struct {
                        uint8_t player_num;
                        uint8_t flags;
                } player_flags_changed;

                struct {
                        uint8_t player_num;
                } player_shouted;

                struct {
                        uint8_t num;
                        uint8_t last_player_moved;
                        int16_t x, y;
                        uint32_t letter;
                } tile_changed;

                struct {
                        int n_tiles;
                } n_tiles_changed;

                struct {
                        const char *code;
                } language_changed;

                struct {
                        bool running;
                } running_state_changed;

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
        VSX_CONNECTION_ERROR_CONNECTION_CLOSED,
        VSX_CONNECTION_ERROR_BAD_PLAYER_ID,
        VSX_CONNECTION_ERROR_BAD_CONVERSATION_ID,
};

extern struct vsx_error_domain
vsx_connection_error;

struct vsx_connection *
vsx_connection_new(void);

/* Resets the connection so that it is like a fresh one returned from
 * vsx_connection_new, except that it doesn’t remove the signal
 * listeners. All of the configuration state is reset except the
 * server address. The connection will no longer be running.
 */
void
vsx_connection_reset(struct vsx_connection *connection);

void
vsx_connection_set_player_name(struct vsx_connection *connection,
                               const char *player_name);

void
vsx_connection_set_room(struct vsx_connection *connection,
                        const char *room);

void
vsx_connection_set_conversation_id(struct vsx_connection *connection,
                                   uint64_t conversation_id);

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

bool
vsx_connection_get_person_id(struct vsx_connection *connection,
                             uint64_t *person_id);

void
vsx_connection_set_person_id(struct vsx_connection *connection,
                             uint64_t person_id);

void
vsx_connection_shout(struct vsx_connection *connection);

void
vsx_connection_turn(struct vsx_connection *connection);

void
vsx_connection_move_tile(struct vsx_connection *connection,
                         int tile_num,
                         int x, int y);

void
vsx_connection_set_n_tiles(struct vsx_connection *connection,
                           int n_tiles);

void
vsx_connection_set_language(struct vsx_connection *connection,
                            const char *language);

void
vsx_connection_send_message(struct vsx_connection *connection,
                            const char *message);

void
vsx_connection_leave(struct vsx_connection *connection);

struct vsx_signal *
vsx_connection_get_event_signal(struct vsx_connection *connection);

void
vsx_connection_set_address(struct vsx_connection *connection,
                           const struct vsx_netaddress *address);

void
vsx_connection_copy_event(struct vsx_connection_event *dest,
                          const struct vsx_connection_event *src);

void
vsx_connection_destroy_event(struct vsx_connection_event *event);

void
vsx_connection_free(struct vsx_connection *connection);

#endif /* VSX_CONNECTION_H */
