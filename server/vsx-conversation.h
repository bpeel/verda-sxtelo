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

#ifndef VSX_CONVERSATION_H
#define VSX_CONVERSATION_H

#include <stdbool.h>

#include "vsx-player.h"
#include "vsx-signal.h"
#include "vsx-object.h"
#include "vsx-tile-data.h"
#include "vsx-buffer.h"

#define VSX_CONVERSATION_MAX_PLAYERS 32

/* Time in microseconds after someone shouts before someone is allowed
 * to shout again */
#define VSX_CONVERSATION_SHOUT_TIME (10 * 1000 * 1000)

typedef struct
{
  VsxObject parent;

  struct vsx_signal changed_signal;

  enum
  {
    VSX_CONVERSATION_AWAITING_START,
    VSX_CONVERSATION_IN_PROGRESS
  } state;

  /* Array of VsxConversationMessage */
  struct vsx_buffer messages;

  int n_players;
  int n_connected_players;
  VsxPlayer *players[VSX_CONVERSATION_MAX_PLAYERS];

  /* Number of tiles that have been added to the game */
  int n_tiles_in_play;
  /* Total number of tiles that will be used */
  int total_n_tiles;
  VsxTile tiles[VSX_TILE_DATA_N_TILES];

  int64_t last_shout_time;

  int log_id;
} VsxConversation;

typedef struct
{
  unsigned int player_num;
  char *text;
} VsxConversationMessage;

typedef enum
{
  VSX_CONVERSATION_STATE_CHANGED,
  VSX_CONVERSATION_N_TILES_CHANGED,
  VSX_CONVERSATION_MESSAGE_ADDED,
  VSX_CONVERSATION_PLAYER_CHANGED,
  VSX_CONVERSATION_TILE_CHANGED,
  VSX_CONVERSATION_SHOUTED
} VsxConversationChangedType;

typedef struct
{
  VsxConversation *conversation;
  VsxConversationChangedType type;
  int num;
} VsxConversationChangedData;

static inline int
vsx_conversation_get_n_messages (VsxConversation *conversation)
{
  return conversation->messages.length / sizeof (VsxConversationMessage);
}

static inline VsxConversationMessage *
vsx_conversation_get_message (VsxConversation *conversation,
                              int message_num)
{
  return (VsxConversationMessage *) conversation->messages.data + message_num;
}

VsxConversation *
vsx_conversation_new (const char *room_name);

void
vsx_conversation_set_n_tiles (VsxConversation *conversation,
                              unsigned int player_num,
                              int n_tiles);

void
vsx_conversation_start (VsxConversation *conversation);

void
vsx_conversation_add_message (VsxConversation *conversation,
                              unsigned int player_num,
                              const char *buffer,
                              unsigned int length);

void
vsx_conversation_set_typing (VsxConversation *conversation,
                             unsigned int player_num,
                             bool typing);

void
vsx_conversation_player_left (VsxConversation *conversation,
                              unsigned int player_num);

VsxPlayer *
vsx_conversation_add_player (VsxConversation *conversation,
                             const char *player_name);

void
vsx_conversation_move_tile (VsxConversation *conversation,
                            unsigned int player_num,
                            int tile_num,
                            int x,
                            int y);

void
vsx_conversation_shout (VsxConversation *conversation,
                        unsigned int player_num);

void
vsx_conversation_turn (VsxConversation *conversation,
                       unsigned int player_num);

#endif /* VSX_CONVERSATION_H */
