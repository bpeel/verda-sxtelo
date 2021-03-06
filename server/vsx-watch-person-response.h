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

#ifndef __VSX_WATCH_PERSON_RESPONSE_H__
#define __VSX_WATCH_PERSON_RESPONSE_H__

#include "vsx-response.h"
#include "vsx-person.h"
#include "vsx-flags.h"
#include "vsx-main-context.h"

G_BEGIN_DECLS

typedef enum
{
  VSX_WATCH_PERSON_RESPONSE_WRITING_HTTP_HEADER,
  VSX_WATCH_PERSON_RESPONSE_WRITING_HEADER,
  VSX_WATCH_PERSON_RESPONSE_AWAITING_DATA,
  VSX_WATCH_PERSON_RESPONSE_WRITING_N_TILES,
  VSX_WATCH_PERSON_RESPONSE_WRITING_NAME,
  VSX_WATCH_PERSON_RESPONSE_WRITING_PLAYER,
  VSX_WATCH_PERSON_RESPONSE_WRITING_SHOUT,
  VSX_WATCH_PERSON_RESPONSE_WRITING_TILE,
  VSX_WATCH_PERSON_RESPONSE_WRITING_MESSAGES,
  VSX_WATCH_PERSON_RESPONSE_WRITING_SYNC,
  VSX_WATCH_PERSON_RESPONSE_WRITING_END,
  VSX_WATCH_PERSON_RESPONSE_WRITING_KEEP_ALIVE,

  VSX_WATCH_PERSON_RESPONSE_DONE
} VsxWatchPersonResponseState;

typedef struct
{
  VsxResponse parent;

  VsxPerson *person;

  VsxListener conversation_changed_listener;

  VsxWatchPersonResponseState state;

  unsigned int message_num;
  unsigned int message_pos;

  /* Number of players that we've sent a "player-name" event for */
  unsigned int named_players;

  union
  {
    /* The number of tiles that was current when we started sending
     * it */
    int n_tiles;

    /* The state that was current when we started sending the player
     * state */
    struct
    {
      unsigned int num;
      VsxPlayerFlags flags;
    } player;

    /* Same for the tile state */
    struct
    {
      unsigned int num;
      gint16 x, y;
      gint16 last_player;
    } tile;
  } dirty;

  /* Bit mask of players whose state needs updating */
  unsigned long dirty_players
  [VSX_FLAGS_N_LONGS_FOR_SIZE (VSX_CONVERSATION_MAX_PLAYERS)];

  /* Bit mask of tiles that need updating */
  unsigned long dirty_tiles
  [VSX_FLAGS_N_LONGS_FOR_SIZE (VSX_TILE_DATA_N_TILES)];

  gboolean last_typing_state;

  int pending_shout;

  gboolean pending_n_tiles;

  gboolean sync_sent;

  gint64 last_write_time;
  VsxMainContextSource *keep_alive_timer;
} VsxWatchPersonResponse;

VsxResponse *
vsx_watch_person_response_new (VsxPerson *person,
                               int last_message);

G_END_DECLS

#endif /* __VSX_WATCH_PERSON_RESPONSE_H__ */
