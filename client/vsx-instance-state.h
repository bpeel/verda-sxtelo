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

/* This represents the state that needs to be preserved in the android
 * application when stopping the process in order to bring the game
 * back up to its previous state when it is restarted. It can be
 * serialised into a string.
 */

struct vsx_instance_state {
        bool has_person_id;
        uint64_t person_id;

        bool invite_visible;
};

void
vsx_instance_state_init(struct vsx_instance_state *state);

char *
vsx_instance_state_save(const struct vsx_instance_state *state);

void
vsx_instance_state_load(struct vsx_instance_state *state,
                        const char *save_data);

#endif /* VSX_INSTANCE_STATE_H */
