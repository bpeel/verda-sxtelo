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
#define VSX_PERSON_SILENCE_TIME (60 * 5 * (gint64) 1000000)

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

  vsx_object_get_class ()->free (object);
}

static const VsxObjectClass *
vsx_person_get_class (void)
{
  static VsxObjectClass klass;

  if (klass.free == NULL)
    {
      klass = *vsx_object_get_class ();
      klass.instance_size = sizeof (VsxPerson);
      klass.free = vsx_person_free;
    }

  return &klass;
}

gboolean
vsx_person_id_equal (gconstpointer v1,
                     gconstpointer v2)
{
  return *(const VsxPersonId *) v1 == *(const VsxPersonId *) v2;
}

guint
vsx_person_id_hash (gconstpointer v)
{
  G_STATIC_ASSERT (sizeof (VsxPersonId) == sizeof (gint64));

  return g_int64_hash (v);
}

VsxPersonId
vsx_person_generate_id (GSocketAddress *address)
{
  VsxPersonId id = 0;
  int i;

  /* Generate enough random numbers to fill the id */
  for (i = 0; i < sizeof (id) / sizeof (guint32); i++)
    id |= (VsxPersonId) g_random_int () << (i * sizeof (guint32) * 8);

  if (address)
    {
      gssize native_size = g_socket_address_get_native_size (address);
      guint8 address_buf[native_size];

      /* XOR the bytes of the connection address so that even if
         someone can work out the sequence of random numbers it's
         still hard to predict what the next id will be */

      if (g_socket_address_to_native (address,
                                      address_buf,
                                      native_size,
                                      NULL /* error */))
        {
          int address_pos = 0;
          guint8 *p = (guint8 *) &id;

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

gboolean
vsx_person_parse_id (const char *string,
                     VsxPersonId *id)
{
  const char *p;

  *id = 0;

  for (p = string; *p; p++)
    if (!g_ascii_isxdigit (*p))
      return FALSE;
    else
      *id = (*id << 4) | g_ascii_xdigit_value (*p);

  return p - string == sizeof (VsxPersonId) * 2;
}

VsxPerson *
vsx_person_new (VsxPersonId id,
                const char *player_name,
                VsxConversation *conversation)
{
  VsxPerson *person =
    vsx_object_allocate (vsx_person_get_class ());

  vsx_object_init (person);

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

gboolean
vsx_person_is_silent (VsxPerson *person)
{
  return ((vsx_main_context_get_monotonic_clock (NULL)
           - person->last_noise_time)
          > VSX_PERSON_SILENCE_TIME);
}
