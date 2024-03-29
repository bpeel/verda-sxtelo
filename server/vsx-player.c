/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2011, 2013  Neil Roberts
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

#include "vsx-player.h"

#include <string.h>

#include "vsx-util.h"

void
vsx_player_free (VsxPlayer *player)
{
  vsx_free (player);
}

VsxPlayer *
vsx_player_new (const char *player_name,
                unsigned int num)
{
  size_t name_len = strlen (player_name);
  VsxPlayer *player = vsx_alloc (offsetof (VsxPlayer, name)
                                 + name_len + 1);

  memcpy (player->name, player_name, name_len + 1);
  player->num = num;

  player->flags = VSX_PLAYER_CONNECTED;

  return player;
}
