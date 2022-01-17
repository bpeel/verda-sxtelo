/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2011, 2013, 2021  Neil Roberts
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

#include "vsx-conversation-set.h"

#include <string.h>

#include "vsx-log.h"
#include "vsx-list.h"
#include "vsx-hash-table.h"
#include "vsx-generate-id.h"

typedef struct
{
  struct vsx_list link;

  /* This is set if the conversation is open to everyone who knows the
   * room name. It becomes NULL when the game starts in order to avoid
   * joining a game that has already started.
   */
  char *room_name;

  VsxConversation *conversation;
  VsxConversationSet *set;
  struct vsx_listener conversation_changed_listener;
} VsxConversationSetListener;

struct _VsxConversationSet
{
  VsxObject parent;

  struct vsx_hash_table hash_table;

  /* List of conversations that have a room name and can still be
   * joined. Once the game starts or can no longer be joined the
   * listener will move to the other list so this one can be quickly
   * scanned for pending games.
   */
  struct vsx_list pending_listeners;
  /* All the other conversations */
  struct vsx_list other_listeners;
};

static void
remove_listener (VsxConversationSetListener *listener)
{
  vsx_list_remove (&listener->link);
  vsx_list_remove (&listener->conversation_changed_listener.link);
  vsx_hash_table_remove (&listener->set->hash_table,
                         &listener->conversation->hash_entry);
  vsx_object_unref (listener->conversation);
  vsx_free (listener->room_name);
  vsx_free (listener);
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
conversation_changed_cb (struct vsx_listener *listener,
                         void *user_data)
{
  VsxConversationSetListener *c_listener =
    vsx_container_of (listener,
                      VsxConversationSetListener,
                      conversation_changed_listener);
  VsxConversationChangedData *data = user_data;

  /* If the conversation has started then we’ll mark it as no longer
   * pending so that no new players can join. People who have the
   * conversation ID and who specifically want to join this game still
   * can though, even after it has started.
   */
  if (c_listener->room_name
      && data->conversation->state != VSX_CONVERSATION_AWAITING_START)
    {
      vsx_list_remove (&c_listener->link);
      vsx_list_insert (&c_listener->set->other_listeners, &c_listener->link);
      vsx_free (c_listener->room_name);
      c_listener->room_name = NULL;
    }

  if (data->type == VSX_CONVERSATION_PLAYER_CHANGED &&
      conversation_is_empty (data->conversation))
    {
      /* If everyone has left the game then we’ll abandon it to avoid
       * leaking it.
       */
      if (data->conversation->state == VSX_CONVERSATION_AWAITING_START)
        {
          vsx_log ("Game %i abandoned without starting",
                   data->conversation->log_id);
        }
      else
        {
          vsx_log ("Freed game %i after everyone left",
                   data->conversation->log_id);
        }

      remove_listener (c_listener);
    }
}

static void
remove_listeners (struct vsx_list *list)
{
  VsxConversationSetListener *listener, *tmp;

  vsx_list_for_each_safe (listener, tmp, list, link)
    {
      remove_listener (listener);
    }
}

static void
vsx_conversation_set_free (void *object)
{
  VsxConversationSet *self = object;

  remove_listeners (&self->pending_listeners);
  remove_listeners (&self->other_listeners);

  vsx_hash_table_destroy (&self->hash_table);

  vsx_free (self);
}

static const VsxObjectClass
vsx_conversation_set_class =
{
  .free = vsx_conversation_set_free,
};

static const VsxTileData *
get_tile_data_for_room_name (const char *room_name)
{
  const char *colon = strchr (room_name, ':');
  int i;

  /* The language code can be specified by prefixing the room name
   * separated by a colon. If we didn’t find one then just use the
   * first tile set. */
  if (colon == NULL)
    return vsx_tile_data;

  /* Look for some tile data for the corresponding room */
  for (i = 0; i < VSX_TILE_DATA_N_ROOMS; i++)
    {
      const VsxTileData *tile_data = vsx_tile_data + i;
      if (strlen (tile_data->language_code) == colon - room_name &&
          !memcmp (tile_data->language_code, room_name, colon - room_name))
        return tile_data;
    }

  /* No language found, just use the first one */
  return vsx_tile_data;
}

static const VsxTileData *
get_tile_data_for_language_code (const char *language_code)
{
  const VsxTileData *tile_data =
    vsx_tile_data_get_for_language_code (language_code);

  if (tile_data == NULL)
    {
      /* No language found, just use the first one */
      return vsx_tile_data;
    }

  return tile_data;
}

VsxConversationSet *
vsx_conversation_set_new (void)
{
  VsxConversationSet *self = vsx_calloc (sizeof *self);

  vsx_object_init (self, &vsx_conversation_set_class);

  vsx_list_init (&self->pending_listeners);
  vsx_list_init (&self->other_listeners);
  vsx_hash_table_init (&self->hash_table);

  return self;
}

static VsxConversationSetListener *
generate_conversation (VsxConversationSet *set,
                       const VsxTileData *tile_data,
                       const struct vsx_netaddress *addr)
{
  VsxConversationId id;

  /* Keep generating ids until we find one that isn't used. It's
   * hopefully pretty unlikely that it will generate a clash.
   */
  do
    id = vsx_generate_id (addr);
  while (vsx_hash_table_get (&set->hash_table, id));

  VsxConversationSetListener *listener = vsx_alloc (sizeof *listener);

  listener->conversation = vsx_conversation_new (id, tile_data);

  listener->room_name = NULL;
  listener->set = set;

  /* Listen for the changed signal so we can remove the conversation
     from the list once the game has begun */
  listener->conversation_changed_listener.notify =
    conversation_changed_cb;
  vsx_signal_add (&listener->conversation->changed_signal,
                  &listener->conversation_changed_listener);

  vsx_hash_table_add (&set->hash_table, &listener->conversation->hash_entry);

  return listener;
}

VsxConversation *
vsx_conversation_set_generate_conversation (VsxConversationSet *set,
                                            const char *language_code,
                                            const struct vsx_netaddress *addr)
{
  const VsxTileData *tile_data = get_tile_data_for_language_code(language_code);

  VsxConversationSetListener *listener =
    generate_conversation (set, tile_data, addr);

  vsx_list_insert (&set->other_listeners, &listener->link);

  return vsx_object_ref (listener->conversation);
}

VsxConversation *
vsx_conversation_set_get_conversation (VsxConversationSet *set,
                                       VsxConversationId id)
{
  struct vsx_hash_table_entry *entry =
    vsx_hash_table_get (&set->hash_table, id);

  return entry ? vsx_container_of (entry, VsxConversation, hash_entry) : NULL;
}

VsxConversation *
vsx_conversation_set_get_pending_conversation (VsxConversationSet *set,
                                               const char *room_name,
                                               const struct vsx_netaddress *add)
{
  VsxConversationSetListener *listener;

  vsx_list_for_each (listener, &set->pending_listeners, link)
    {
      if (!strcmp (listener->room_name, room_name))
        return vsx_object_ref (listener->conversation);
    }

  const VsxTileData *tile_data = get_tile_data_for_room_name (room_name);

  /* If there's no conversation with that name then we'll create it */
  listener = generate_conversation (set, tile_data, add);

  vsx_list_insert (&set->pending_listeners, &listener->link);

  listener->room_name = vsx_strdup (room_name);

  return vsx_object_ref (listener->conversation);
}
