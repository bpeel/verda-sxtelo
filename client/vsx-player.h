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

#include <glib.h>
#include <stdbool.h>

G_BEGIN_DECLS

typedef struct _VsxPlayer VsxPlayer;

int
vsx_player_get_number (const VsxPlayer *player);

const char *
vsx_player_get_name (const VsxPlayer *player);

bool
vsx_player_is_connected (const VsxPlayer *player);

bool
vsx_player_is_typing (const VsxPlayer *player);

bool
vsx_player_has_next_turn (const VsxPlayer *player);

G_END_DECLS

#endif /* VSX_PLAYER_H */
