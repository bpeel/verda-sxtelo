/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2013  Neil Roberts
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

#ifndef VSX_PLAYER_H
#define VSX_PLAYER_H

#include <stdbool.h>

struct vsx_player;

int
vsx_player_get_number(const struct vsx_player *player);

const char *
vsx_player_get_name(const struct vsx_player *player);

bool
vsx_player_is_connected(const struct vsx_player *player);

bool
vsx_player_is_typing(const struct vsx_player *player);

bool
vsx_player_is_shouting(const struct vsx_player *player);

bool
vsx_player_has_next_turn(const struct vsx_player *player);

#endif /* VSX_PLAYER_H */
