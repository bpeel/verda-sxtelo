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

#include "config.h"

#include "vsx-player-private.h"

bool
vsx_player_is_connected (const VsxPlayer *player)
{
  return !!(player->flags & VSX_PLAYER_CONNECTED);
}

bool
vsx_player_is_typing (const VsxPlayer *player)
{
  return !!(player->flags & VSX_PLAYER_TYPING);
}

bool
vsx_player_has_next_turn (const VsxPlayer *player)
{
  return !!(player->flags & VSX_PLAYER_NEXT_TURN);
}

int
vsx_player_get_number (const VsxPlayer *player)
{
  return player->num;
}

const char *
vsx_player_get_name (const VsxPlayer *player)
{
  return player->name ? player->name : "";
}
