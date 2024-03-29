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

#ifndef VSX_PLAYER_H
#define VSX_PLAYER_H

#include <stdbool.h>

typedef enum
{
  VSX_PLAYER_CONNECTED = (1 << 0),
  VSX_PLAYER_TYPING = (1 << 1),
  VSX_PLAYER_NEXT_TURN = (1 << 2)
} VsxPlayerFlags;

typedef struct
{
  /* First person to join a conversation gets the number 0, the second
   * 1 and so on */
  unsigned int num;

  VsxPlayerFlags flags;

  /* Over-allocated */
  char name[1];
} VsxPlayer;

static inline bool
vsx_player_is_connected (const VsxPlayer *player)
{
  return !!(player->flags & VSX_PLAYER_CONNECTED);
}

static inline bool
vsx_player_is_typing (const VsxPlayer *player)
{
  return !!(player->flags & VSX_PLAYER_TYPING);
}

static inline bool
vsx_player_has_next_turn (const VsxPlayer *player)
{
  return !!(player->flags & VSX_PLAYER_NEXT_TURN);
}

VsxPlayer *
vsx_player_new (const char *player_name,
                unsigned int num);

void
vsx_player_free (VsxPlayer *player);

#endif /* VSX_PLAYER_H */
