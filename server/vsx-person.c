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

  g_free (person);
}

static const VsxObjectClass
vsx_person_class =
  {
    .free = vsx_person_free,
  };

VsxPersonId
vsx_person_generate_id (GSocketAddress *address)
{
  VsxPersonId id = 0;
  int i;

  /* Generate enough random numbers to fill the id */
  for (i = 0; i < sizeof (id) / sizeof (uint32_t); i++)
    id |= (VsxPersonId) g_random_int () << (i * sizeof (uint32_t) * 8);

  if (address)
    {
      gssize native_size = g_socket_address_get_native_size (address);
      uint8_t address_buf[native_size];

      /* XOR the bytes of the connection address so that even if
         someone can work out the sequence of random numbers it's
         still hard to predict what the next id will be */

      if (g_socket_address_to_native (address,
                                      address_buf,
                                      native_size,
                                      NULL /* error */))
        {
          int address_pos = 0;
          uint8_t *p = (uint8_t *) &id;

          for (i = 0; i < sizeof (id); i++)
            {
              p[i] ^= address_buf[address_pos];
              if (++address_pos >= native_size)
                address_pos = 0;
            }
        }
      else
        g_warning ("g_socket_address_to_native failed");
    }

  return id;
}

VsxPerson *
vsx_person_new (VsxPersonId id,
                const char *player_name,
                VsxConversation *conversation)
{
  VsxPerson *person = g_new0 (VsxPerson, 1);

  vsx_object_init (person, &vsx_person_class);

  vsx_person_make_noise (person);

  person->id = id;
  person->conversation = vsx_object_ref (conversation);
  person->message_offset = conversation->messages->len;

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
