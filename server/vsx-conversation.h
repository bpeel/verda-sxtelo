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

#ifndef __VSX_CONVERSATION_H__
#define __VSX_CONVERSATION_H__

#include <glib.h>

#include "vsx-player.h"
#include "vsx-signal.h"
#include "vsx-object.h"

G_BEGIN_DECLS

#define VSX_CONVERSATION_MAX_PLAYERS 32

typedef struct
{
  VsxObject parent;

  VsxSignal changed_signal;
  VsxSignal player_changed_signal;

  enum
  {
    VSX_CONVERSATION_AWAITING_PARTNER,
    VSX_CONVERSATION_IN_PROGRESS,
    VSX_CONVERSATION_FINISHED
  } state;

  GArray *messages;

  /* Bitmask of people that are currently typing */
  unsigned int typing_mask;

  int n_players;
  VsxPlayer *players[VSX_CONVERSATION_MAX_PLAYERS];
} VsxConversation;

typedef struct
{
  unsigned int length;
  char *text;
} VsxConversationMessage;

VsxConversation *
vsx_conversation_new (void);

void
vsx_conversation_start (VsxConversation *conversation);

void
vsx_conversation_finish (VsxConversation *conversation);

void
vsx_conversation_add_message (VsxConversation *conversation,
                              unsigned int person_num,
                              const char *buffer,
                              unsigned int length);

void
vsx_conversation_set_typing (VsxConversation *conversation,
                             unsigned int person_num,
                             gboolean typing);

VsxPlayer *
vsx_conversation_add_player (VsxConversation *conversation,
                             const char *player_name);

G_END_DECLS

#endif /* __VSX_CONVERSATION_H__ */
