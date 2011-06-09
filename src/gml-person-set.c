/*
 * Gemelo - A server for chatting with strangers in a foreign language
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

#include <glib-object.h>

#include "gml-person-set.h"

G_DEFINE_TYPE (GmlPersonSet, gml_person_set, G_TYPE_OBJECT);

static void
gml_person_set_finalize (GObject *object)
{
  GmlPersonSet *self = (GmlPersonSet *) object;

  g_hash_table_destroy (self->hash_table);

  G_OBJECT_CLASS (gml_person_set_parent_class)->finalize (object);
}

static void
gml_person_set_class_init (GmlPersonSetClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gml_person_set_finalize;
}

static void
gml_person_set_init (GmlPersonSet *self)
{
  self->hash_table = g_hash_table_new_full (gml_person_id_hash,
                                            gml_person_id_equal,
                                            NULL,
                                            (GDestroyNotify) g_object_unref);
}

GmlPersonSet *
gml_person_set_new (void)
{
  GmlPersonSet *self = g_object_new (GML_TYPE_PERSON_SET, NULL);

  return self;
}

GmlPerson *
gml_person_set_get_person (GmlPersonSet *set,
                           GmlPersonId id)
{
  return g_hash_table_lookup (set->hash_table, &id);
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

  g_hash_table_insert (set->hash_table, &person->id, g_object_ref (person));

  return person;
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
  g_hash_table_foreach_remove (set->hash_table,
                               remove_useless_people_cb,
                               NULL);
}
