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

#ifndef VSX_GAME_PAINTER_H
#define VSX_GAME_PAINTER_H

#include "vsx-error.h"
#include "vsx-main-thread.h"
#include "vsx-gl.h"
#include "vsx-game-state.h"
#include "vsx-asset.h"
#include "vsx-shell-interface.h"

/* The game painter is the main painter object that owns all of the
 * other painters. It should only contain resources needed for
 * painting and no game state. That way the painter can be destroyed
 * and recreated without changing what is painted.
 */

struct vsx_game_painter;

struct vsx_game_painter *
vsx_game_painter_new(struct vsx_gl *gl,
                     struct vsx_main_thread *main_thread,
                     struct vsx_game_state *game_state,
                     struct vsx_asset_manager *asset_manager,
                     int dpi,
                     struct vsx_shell_interface *shell,
                     struct vsx_error **error);

void
vsx_game_painter_set_fb_size(struct vsx_game_painter *painter,
                             int width,
                             int height);

void
vsx_game_painter_paint(struct vsx_game_painter *painter);

void
vsx_game_painter_press_finger(struct vsx_game_painter *painter,
                              int finger,
                              int x,
                              int y);

void
vsx_game_painter_release_finger(struct vsx_game_painter *painter,
                                int finger);

void
vsx_game_painter_move_finger(struct vsx_game_painter *painter,
                             int finger,
                             int x,
                             int y);

void
vsx_game_painter_cancel_gesture(struct vsx_game_painter *painter);

void
vsx_game_painter_free(struct vsx_game_painter *painter);

#endif /* VSX_GAME_PAINTER_H */
