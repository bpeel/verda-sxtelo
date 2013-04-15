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

#ifndef __VSX_PLAYER_H__
#define __VSX_PLAYER_H__

#include <glib.h>

G_BEGIN_DECLS

typedef struct
{
  /* First person to join a conversation gets the number 0, the second
   * 1 and so on */
  unsigned int num;

  char *name;

  gsize name_message_len;
  char *name_message;
} VsxPlayer;

VsxPlayer *
vsx_player_new (const char *player_name,
                unsigned int num);

void
vsx_player_free (VsxPlayer *player);

G_END_DECLS

#endif /* __VSX_PLAYER_H__ */
