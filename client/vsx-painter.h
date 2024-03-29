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

#ifndef VSX_PAINTER_H
#define VSX_PAINTER_H

#include <stdbool.h>

#include "vsx-toolbox.h"
#include "vsx-game-state.h"
#include "vsx-signal.h"
#include "vsx-input-event.h"

struct vsx_painter {
        void *
        (* create_cb)(struct vsx_game_state *game_state,
                      struct vsx_toolbox *toolbox);

        void
        (* fb_size_changed_cb)(void *painter);

        void
        (* prepare_cb)(void *painter);

        void
        (* paint_cb)(void *painter);

        bool
        (* input_event_cb)(void *painter,
                           const struct vsx_input_event *event);

        void
        (* free_cb)(void *painter);
};

#endif /* VSX_PAINTER_H */
