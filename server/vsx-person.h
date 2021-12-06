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

#ifndef __VSX_PERSON_H__
#define __VSX_PERSON_H__

#include <stdint.h>
#include <stdbool.h>

#include "vsx-conversation.h"
#include "vsx-player.h"
#include "vsx-signal.h"
#include "vsx-list.h"

typedef uint64_t VsxPersonId;

typedef struct _VsxPerson VsxPerson;

struct _VsxPerson
{
  VsxObject parent;

  /* Position in the global list of people */
  VsxList link;

  /* Used to implement the hash table */
  VsxPerson *hash_next;

  VsxPersonId id;

  VsxConversation *conversation;

  VsxPlayer *player;

  int64_t last_noise_time;

  /* When a player joins this number is set to the current number of
   * messages. Any reference to a message number sent from the client
   * is offset by this number so that they can't refer to any messages
   * that were sent before they joined. */
  unsigned int message_offset;
};

VsxPerson *
vsx_person_new (VsxPersonId id,
                const char *player_name,
                VsxConversation *conversation);

void
vsx_person_make_noise (VsxPerson *person);

bool
vsx_person_is_silent (VsxPerson *person);

void
vsx_person_leave_conversation (VsxPerson *person);

#endif /* __VSX_PERSON_H__ */
