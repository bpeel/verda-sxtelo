/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
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

#include "vsx-person-set.h"

G_DEFINE_TYPE (VsxPersonSet, vsx_person_set, G_TYPE_OBJECT);

static void
vsx_person_set_finalize (GObject *object)
{
  VsxPersonSet *self = (VsxPersonSet *) object;

  g_hash_table_destroy (self->hash_table);

  G_OBJECT_CLASS (vsx_person_set_parent_class)->finalize (object);
}

static void
vsx_person_set_class_init (VsxPersonSetClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = vsx_person_set_finalize;
}

static void
vsx_person_set_init (VsxPersonSet *self)
{
  self->hash_table = g_hash_table_new_full (vsx_person_id_hash,
                                            vsx_person_id_equal,
                                            NULL,
                                            (GDestroyNotify) g_object_unref);
}

VsxPersonSet *
vsx_person_set_new (void)
{
  VsxPersonSet *self = g_object_new (VSX_TYPE_PERSON_SET, NULL);

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

  g_hash_table_insert (set->hash_table, &person->id, g_object_ref (person));

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
        vsx_conversation_finish (person->conversation);

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
