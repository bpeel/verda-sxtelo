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

#include "config.h"

#include <assert.h>

#include "vsx-person-set.h"
#include "vsx-generate-id.h"
#include "vsx-list.h"
#include "vsx-util.h"
#include "vsx-hash-table.h"

#define VSX_PERSON_SET_REMOVE_SILENT_PEOPLE_INTERVAL 5

struct _VsxPersonSet
{
  VsxObject parent;

  struct vsx_list people;

  struct vsx_hash_table hash_table;

  VsxMainContextSource *people_timer_source;
};

static void
vsx_person_set_free (void *object)
{
  VsxPersonSet *self = object;

  VsxPerson *person, *tmp;

  vsx_hash_table_destroy (&self->hash_table);

  vsx_list_for_each_safe (person, tmp, &self->people, link)
    {
      vsx_object_unref (person);
    }

  if (self->people_timer_source)
    vsx_main_context_remove_source (self->people_timer_source);

  vsx_free (self);
}

static const VsxObjectClass
vsx_person_set_class =
  {
    .free = vsx_person_set_free,
  };

static void
remove_person (VsxPersonSet *set,
               VsxPerson *person)
{
  if (person->conversation)
    vsx_person_leave_conversation (person);

  vsx_list_remove (&person->link);

  vsx_hash_table_remove (&set->hash_table, &person->hash_entry);

  vsx_object_unref (person);
}

static void
remove_silent_people_timer_cb (VsxMainContextSource *source,
                               void *user_data)
{
  VsxPersonSet *set = user_data;

  /* This is probably relatively expensive because it has to iterate
     the entire list of people, but it only happens infrequently so
     hopefully it's not a problem */

  VsxPerson *person, *tmp;

  vsx_list_for_each_safe (person, tmp, &set->people, link)
    {
      if (vsx_person_is_silent (person))
        remove_person (set, person);
    }

  if (vsx_list_empty (&set->people))
    {
      vsx_main_context_remove_source (source);
      set->people_timer_source = NULL;
    }
}

VsxPersonSet *
vsx_person_set_new (void)
{
  VsxPersonSet *self = vsx_calloc (sizeof *self);

  vsx_object_init (self, &vsx_person_set_class);

  vsx_list_init (&self->people);

  vsx_hash_table_init (&self->hash_table);

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
  struct vsx_hash_table_entry *entry =
    vsx_hash_table_get (&set->hash_table, id);

  return entry ? vsx_container_of (entry, VsxPerson, hash_entry) : NULL;
}

VsxPerson *
vsx_person_set_generate_person (VsxPersonSet *set,
                                const char *player_name,
                                const struct vsx_netaddress *address,
                                VsxConversation *conversation)
{
  VsxPerson *person;
  VsxPersonId id;

  /* Keep generating ids until we find one that isn't used. It's
     hopefully pretty unlikely that it will generate a clash */
  do
    id = vsx_generate_id (address);
  while (vsx_hash_table_get (&set->hash_table, id));

  person = vsx_person_new (id, player_name, conversation);

  vsx_list_insert (&set->people, &person->link);

  vsx_hash_table_add (&set->hash_table, &person->hash_entry);

  vsx_object_ref (person);

  if (set->people_timer_source == NULL)
    set->people_timer_source =
      vsx_main_context_add_timer (NULL, /* default context */
                                  VSX_PERSON_SET_REMOVE_SILENT_PEOPLE_INTERVAL,
                                  remove_silent_people_timer_cb,
                                  set);

  return person;
}
