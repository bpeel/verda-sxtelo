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
#include <assert.h>

#include "vsx-person-set.h"
#include "vsx-generate-id.h"
#include "vsx-list.h"
#include "vsx-util.h"

#define VSX_PERSON_SET_REMOVE_SILENT_PEOPLE_INTERVAL 5

struct _VsxPersonSet
{
  VsxObject parent;

  VsxList people;

  int n_people;
  int hash_size;
  VsxPerson **hash_table;

  VsxMainContextSource *people_timer_source;
};

static int
get_hash_pos (VsxPersonSet *set,
              uint64_t id)
{
  return id % set->hash_size;
}

static void
vsx_person_set_free (void *object)
{
  VsxPersonSet *self = object;

  VsxPerson *person, *tmp;

  vsx_list_for_each_safe (person, tmp, &self->people, link)
    {
      vsx_object_unref (person);
    }

  vsx_free (self->hash_table);

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

  int pos = get_hash_pos (set, person->id);
  VsxPerson **prev = set->hash_table + pos;
  while (true)
    {
      assert (*prev);

      if (*prev == person)
        break;

      prev = &(*prev)->hash_next;
    }

  *prev = person->hash_next;

  vsx_object_unref (person);

  set->n_people--;
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

  self->hash_size = 8;
  self->hash_table = vsx_calloc (self->hash_size
                                 * sizeof *self->hash_table);

  return self;
}

static void
add_person_to_hash (VsxPersonSet *set,
                    VsxPerson *person)
{
  int pos = get_hash_pos (set, person->id);

  person->hash_next = set->hash_table[pos];
  set->hash_table[pos] = person;
}

static void
grow_hash_table (VsxPersonSet *set)
{
  vsx_free (set->hash_table);

  set->hash_size *= 2;

  set->hash_table = vsx_calloc (set->hash_size * sizeof *set->hash_table);

  VsxPerson *person;

  vsx_list_for_each (person, &set->people, link)
    {
      add_person_to_hash (set, person);
    }
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
  int pos = get_hash_pos (set, id);

  for (VsxPerson *person = set->hash_table[pos];
       person;
       person = person->hash_next)
    {
      if (person->id == id)
        return person;
    }

  return NULL;
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
  while (vsx_person_set_get_person (set, id));

  person = vsx_person_new (id, player_name, conversation);

  if ((set->n_people + 1) > set->hash_size * 3 / 4)
    grow_hash_table (set);

  vsx_list_insert (&set->people, &person->link);
  add_person_to_hash (set, vsx_object_ref (person));

  set->n_people++;

  if (set->people_timer_source == NULL)
    set->people_timer_source =
      vsx_main_context_add_timer (NULL, /* default context */
                                  VSX_PERSON_SET_REMOVE_SILENT_PEOPLE_INTERVAL,
                                  remove_silent_people_timer_cb,
                                  set);

  return person;
}
