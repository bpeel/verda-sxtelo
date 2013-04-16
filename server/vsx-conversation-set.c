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

#include "vsx-conversation-set.h"
#include "vsx-log.h"

typedef struct
{
  char *room_name;
  VsxConversationSet *set;
  VsxConversation *conversation;
  VsxListener conversation_changed_listener;
} VsxConversationSetHashData;

static void
free_hash_data (VsxConversationSetHashData *data)
{
  vsx_list_remove (&data->conversation_changed_listener.link);
  vsx_object_unref (data->conversation);
  g_free (data->room_name);
  g_slice_free (VsxConversationSetHashData, data);
}

static void
conversation_changed_cb (VsxListener *listener,
                         void *data)
{
  VsxConversationSetHashData *hash_data =
    vsx_container_of (listener, hash_data, conversation_changed_listener);
  VsxConversation *conversation = data;

  if (conversation->state != VSX_CONVERSATION_AWAITING_START)
    /* This will also destroy the hash data */
    g_hash_table_remove (hash_data->set->hash_table, hash_data->room_name);
}

static void
vsx_conversation_set_free (void *object)
{
  VsxConversationSet *self = object;

  g_hash_table_destroy (self->hash_table);

  vsx_object_get_class ()->free (object);
}

static const VsxObjectClass *
vsx_conversation_set_get_class (void)
{
  static VsxObjectClass klass;

  if (klass.free == NULL)
    {
      klass = *vsx_object_get_class ();
      klass.instance_size = sizeof (VsxConversationSet);
      klass.free = vsx_conversation_set_free;
    }

  return &klass;
}

VsxConversationSet *
vsx_conversation_set_new (void)
{
  VsxConversationSet *self =
    vsx_object_allocate (vsx_conversation_set_get_class ());

  vsx_object_init (self);

  /* The hash table doesn't have a destroy function for the key
     because its owned by the hash data */
  self->hash_table =
    g_hash_table_new_full (g_str_hash,
                           g_str_equal,
                           NULL, /* key_destroy */
                           /* value_destroy */
                           (GDestroyNotify) free_hash_data);

  return self;
}

VsxConversation *
vsx_conversation_set_get_conversation (VsxConversationSet *set,
                                       const char *room_name)
{
  VsxConversationSetHashData *data;
  VsxConversation *conversation;

  if ((data = g_hash_table_lookup (set->hash_table, room_name)) == NULL)
    {
      /* If there's no conversation with that name then we'll create it */
      conversation = vsx_conversation_new ();

      data = g_slice_new (VsxConversationSetHashData);

      data->room_name = g_strdup (room_name);
      data->set = set;
      data->conversation = conversation;

      /* Listen for the changed signal so we can remove the
         conversation from the hash table if the first person leaves
         before a second joins */
      data->conversation_changed_listener.notify =
        conversation_changed_cb;
      vsx_signal_add (&conversation->changed_signal,
                      &data->conversation_changed_listener);

      /* Take a reference for the hash table */
      vsx_object_ref (conversation);

      g_hash_table_insert (set->hash_table,
                           data->room_name,
                           data);

      vsx_log ("New conversation pending in \"%s\"", room_name);
    }
  else
    {
      conversation = vsx_object_ref (data->conversation);
    }

  return conversation;
}
