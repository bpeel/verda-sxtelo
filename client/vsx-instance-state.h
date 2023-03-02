/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2022  Neil Roberts
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

#ifndef VSX_INSTANCE_STATE_H
#define VSX_INSTANCE_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "vsx-dialog.h"

/* This represents the state that needs to be preserved in the android
 * application when stopping the process in order to bring the game
 * back up to its previous state when it is restarted. It can be
 * serialised into a string.
 */

enum vsx_instance_state_id_type {
        /* This is a fresh start of the app and we don’t have any ID
         * to reconnect to.
         */
        VSX_INSTANCE_STATE_ID_TYPE_NONE,
        /* The app has been given an invite URL to connect to but it
         * hasn’t yet been used to connect and get a person ID.
         */
        VSX_INSTANCE_STATE_ID_TYPE_CONVERSATION,
        /* The app has successfully connected to the server and has a
         * player in a game to reconnect to.
         */
        VSX_INSTANCE_STATE_ID_TYPE_PERSON,
};

struct vsx_instance_state {
        enum vsx_instance_state_id_type id_type;
        uint64_t id;

        enum vsx_dialog dialog;

        /* The current page number in the guide */
        int page;
};

void
vsx_instance_state_init(struct vsx_instance_state *state);

char *
vsx_instance_state_save(const struct vsx_instance_state *state);

void
vsx_instance_state_load(struct vsx_instance_state *state,
                        const char *save_data);

#endif /* VSX_INSTANCE_STATE_H */
