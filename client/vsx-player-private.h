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

#ifndef VSX_PLAYER_PRIVATE_H
#define VSX_PLAYER_PRIVATE_H

#include "vsx-player.h"

enum vsx_player_flags
{
  VSX_PLAYER_CONNECTED = (1 << 0),
  VSX_PLAYER_TYPING = (1 << 1),
  VSX_PLAYER_NEXT_TURN = (1 << 2)
};

struct vsx_player
{
  int num;

  char *name;

  enum vsx_player_flags flags;
};

#endif /* VSX_PLAYER_PRIVATE_H */
