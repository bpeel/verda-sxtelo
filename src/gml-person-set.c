/*
 * Gemelo - A person for chatting with strangers in a foreign language
 * Copyright (C) 2011  Neil Roberts
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

#include "gml-person-set.h"

GmlPersonSet *
gml_person_set_new (void)
{
  GmlPersonSet *set;

  set = g_hash_table_new_full (gml_person_id_hash,
                               gml_person_id_equal,
                               NULL,
                               (GDestroyNotify) g_object_unref);

  return set;
}

GmlPerson *
gml_person_set_get_person (GmlPersonSet *set,
                           GmlPersonId id)
{
  return g_hash_table_lookup (set, &id);
}

GmlPerson *
gml_person_set_generate_person (GmlPersonSet *set,
                                GSocketAddress *address,
                                GmlConversation *conversation)
{
  GmlPerson *person;
  GmlPersonId id;

  /* Keep generating ids until we find one that isn't used. It's
     hopefully pretty unlikely that it will generate a clash */
  do
    id = gml_person_generate_id (address);
  while (gml_person_set_get_person (set, id));

  person = gml_person_new (id, conversation);

  g_hash_table_insert (set, &person->id, g_object_ref (person));

  return person;
}

void
gml_person_set_remove_person (GmlPersonSet *set,
                              GmlPerson *person)
{
  gboolean success;

  /* This should also unref the person */
  success = g_hash_table_remove (set, &person->id);

  if (!success)
    g_warning ("Tried to remove a GmlPerson that is not in the GmlPersonSet");
}

void
gml_person_set_free (GmlPersonSet *set)
{
  g_hash_table_destroy (set);
}

static gboolean
remove_useless_people_cb (gpointer key,
                          gpointer value,
                          gpointer user_data)
{
  if (!gml_person_has_use (value))
    {
      gml_person_leave_conversation (value);

      return TRUE;
    }

  return FALSE;
}

void
gml_person_set_remove_useless_people (GmlPersonSet *set)
{
  g_hash_table_foreach_remove (set,
                               remove_useless_people_cb,
                               NULL);
}
