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

#include "vsx-person-set.h"

static void
vsx_person_set_free (void *object)
{
  VsxPersonSet *self = object;

  g_hash_table_destroy (self->hash_table);

  vsx_object_get_class ()->free (object);
}

static const VsxObjectClass *
vsx_person_set_get_class (void)
{
  static VsxObjectClass klass;

  if (klass.free == NULL)
    {
      klass = *vsx_object_get_class ();
      klass.instance_size = sizeof (VsxPersonSet);
      klass.free = vsx_person_set_free;
    }

  return &klass;
}

VsxPersonSet *
vsx_person_set_new (void)
{
  VsxPersonSet *self = vsx_object_allocate (vsx_person_set_get_class ());

  vsx_object_init (self);

  self->hash_table = g_hash_table_new_full (vsx_person_id_hash,
                                            vsx_person_id_equal,
                                            NULL,
                                            (GDestroyNotify) vsx_object_unref);

  return self;
}

VsxPerson *
vsx_person_set_activate_person (VsxPersonSet *set,
                                VsxPersonId id)
{
  VsxPerson *person = vsx_person_set_get_person (set, id);

  if (person)
    vsx_person_make_noise (person);

  return person;
}

VsxPerson *
vsx_person_set_get_person (VsxPersonSet *set,
                           VsxPersonId id)
{
  return g_hash_table_lookup (set->hash_table, &id);
}

VsxPerson *
vsx_person_set_generate_person (VsxPersonSet *set,
                                const char *player_name,
                                GSocketAddress *address,
                                VsxConversation *conversation)
{
  VsxPerson *person;
  VsxPersonId id;

  /* Keep generating ids until we find one that isn't used. It's
     hopefully pretty unlikely that it will generate a clash */
  do
    id = vsx_person_generate_id (address);
  while (vsx_person_set_get_person (set, id));

  person = vsx_person_new (id, player_name, conversation);

  g_hash_table_insert (set->hash_table, &person->id, vsx_object_ref (person));

  return person;
}

static gboolean
remove_silent_people_cb (gpointer key,
                         gpointer value,
                         gpointer user_data)
{
  VsxPerson *person = value;

  if (vsx_person_is_silent (person))
    {
      if (person->conversation)
        vsx_person_leave_conversation (person);

      return TRUE;
    }
  else
    return FALSE;
}

void
vsx_person_set_remove_silent_people (VsxPersonSet *set)
{
  g_hash_table_foreach_remove (set->hash_table,
                               remove_silent_people_cb,
                               NULL);
}
