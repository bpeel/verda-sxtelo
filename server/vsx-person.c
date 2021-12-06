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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <string.h>

#include "vsx-person.h"
#include "vsx-main-context.h"

/* Time in microseconds after the last request is sent on a person
   before he/she is considered to be silent */
#define VSX_PERSON_SILENCE_TIME (60 * 5 * (int64_t) 1000000)

static void
vsx_person_free (void *object)
{
  VsxPerson *person = object;

  if (person->conversation)
    {
      vsx_conversation_player_left (person->conversation,
                                    person->player->num);
      vsx_object_unref (person->conversation);
    }

  vsx_free (person);
}

static const VsxObjectClass
vsx_person_class =
  {
    .free = vsx_person_free,
  };

VsxPerson *
vsx_person_new (VsxPersonId id,
                const char *player_name,
                VsxConversation *conversation)
{
  VsxPerson *person = vsx_calloc (sizeof *person);

  vsx_object_init (person, &vsx_person_class);

  vsx_person_make_noise (person);

  person->id = id;
  person->conversation = vsx_object_ref (conversation);
  person->message_offset = vsx_conversation_get_n_messages (conversation);

  person->player = vsx_conversation_add_player (conversation, player_name);

  return person;
}

void
vsx_person_leave_conversation (VsxPerson *person)
{
  vsx_conversation_player_left (person->conversation,
                                person->player->num);
}

void
vsx_person_make_noise (VsxPerson *person)
{
  person->last_noise_time = vsx_main_context_get_monotonic_clock (NULL);
}

bool
vsx_person_is_silent (VsxPerson *person)
{
  return ((vsx_main_context_get_monotonic_clock (NULL)
           - person->last_noise_time)
          > VSX_PERSON_SILENCE_TIME);
}
