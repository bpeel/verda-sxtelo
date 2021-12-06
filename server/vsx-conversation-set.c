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

struct _VsxConversationSet
{
  VsxObject parent;

  /* Hash table of pending conversations. This only contains
     conversations that only have one person. The key is the name of
     the room and the value is the a VsxServerConversationHashData
     struct (which contains a pointer to the conversation). The hash
     table listens for the changed signal on the conversation so that
     it can remove the conversation if the first person leaves before
     the second person joins. */
  GHashTable *hash_table;
};

static void
free_hash_data (VsxConversationSetHashData *data)
{
  vsx_list_remove (&data->conversation_changed_listener.link);
  vsx_object_unref (data->conversation);
  g_free (data->room_name);
  g_slice_free (VsxConversationSetHashData, data);
}

static bool
conversation_is_empty (VsxConversation *conversation)
{
  int i;

  for (i = 0; i < conversation->n_players; i++)
    if (vsx_player_is_connected (conversation->players[i]))
      return false;

  return true;
}

static void
remove_conversation (VsxConversationSetHashData *hash_data)
{
  /* This will also destroy the hash data */
  g_hash_table_remove (hash_data->set->hash_table, hash_data->room_name);
}

static void
conversation_changed_cb (VsxListener *listener,
                         void *user_data)
{
  VsxConversationSetHashData *hash_data =
    vsx_container_of (listener, hash_data, conversation_changed_listener);
  VsxConversationChangedData *data = user_data;

  /* If the conversation has started then we'll remove it so that no
   * new players can join. */
  if (data->conversation->state != VSX_CONVERSATION_AWAITING_START)
    remove_conversation (hash_data);
  else if (data->type == VSX_CONVERSATION_PLAYER_CHANGED &&
           conversation_is_empty (data->conversation))
    {
      /* We'll also do this if everyone leaves the room because it
       * would be a bit rubbish to join a game where everyone has
       * already left. If we don't do this then conversations that
       * never start would end up leaking */
      vsx_log ("Game %i abandoned without starting",
               data->conversation->id);

      remove_conversation (hash_data);
    }
}

static void
vsx_conversation_set_free (void *object)
{
  VsxConversationSet *self = object;

  g_hash_table_destroy (self->hash_table);

  g_free (self);
}

static const VsxObjectClass
vsx_conversation_set_class =
{
  .free = vsx_conversation_set_free,
};

VsxConversationSet *
vsx_conversation_set_new (void)
{
  VsxConversationSet *self = g_new0 (VsxConversationSet, 1);

  vsx_object_init (self, &vsx_conversation_set_class);

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
      conversation = vsx_conversation_new (room_name);

      data = g_slice_new (VsxConversationSetHashData);

      data->room_name = g_strdup (room_name);
      data->set = set;
      data->conversation = conversation;

      /* Listen for the changed signal so we can remove the
         conversation from the hash table once the game has begun */
      data->conversation_changed_listener.notify =
        conversation_changed_cb;
      vsx_signal_add (&conversation->changed_signal,
                      &data->conversation_changed_listener);

      /* Take a reference for the hash table */
      vsx_object_ref (conversation);

      g_hash_table_insert (set->hash_table,
                           data->room_name,
                           data);
    }
  else
    {
      conversation = vsx_object_ref (data->conversation);
    }

  return conversation;
}
