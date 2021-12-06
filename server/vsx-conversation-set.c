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
#include "vsx-list.h"

typedef struct
{
  VsxList link;
  char *room_name;
  VsxConversation *conversation;
  VsxConversationSet *set;
  VsxListener conversation_changed_listener;
} VsxConversationSetPending;

struct _VsxConversationSet
{
  VsxObject parent;

  VsxList pending_conversations;
};

static void
remove_pending (VsxConversationSetPending *pending)
{
  vsx_list_remove (&pending->link);
  vsx_list_remove (&pending->conversation_changed_listener.link);
  vsx_object_unref (pending->conversation);
  g_free (pending->room_name);
  g_free (pending);
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
conversation_changed_cb (VsxListener *listener,
                         void *user_data)
{
  VsxConversationSetPending *pending =
    vsx_container_of (listener, pending, conversation_changed_listener);
  VsxConversationChangedData *data = user_data;

  /* If the conversation has started then we'll remove it so that no
   * new players can join. */
  if (data->conversation->state != VSX_CONVERSATION_AWAITING_START)
    remove_pending (pending);
  else if (data->type == VSX_CONVERSATION_PLAYER_CHANGED &&
           conversation_is_empty (data->conversation))
    {
      /* We'll also do this if everyone leaves the room because it
       * would be a bit rubbish to join a game where everyone has
       * already left. If we don't do this then conversations that
       * never start would end up leaking */
      vsx_log ("Game %i abandoned without starting",
               data->conversation->id);

      remove_pending (pending);
    }
}

static void
vsx_conversation_set_free (void *object)
{
  VsxConversationSet *self = object;

  VsxConversationSetPending *pending, *tmp;

  vsx_list_for_each_safe (pending, tmp, &self->pending_conversations, link)
    {
      remove_pending (pending);
    }

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

  vsx_list_init (&self->pending_conversations);

  return self;
}

VsxConversation *
vsx_conversation_set_get_conversation (VsxConversationSet *set,
                                       const char *room_name)
{
  VsxConversationSetPending *pending;

  vsx_list_for_each (pending, &set->pending_conversations, link)
    {
      if (!strcmp (pending->room_name, room_name))
        return vsx_object_ref (pending->conversation);
    }

  /* If there's no conversation with that name then we'll create it */
  VsxConversation *conversation = vsx_conversation_new (room_name);

  pending = g_new (VsxConversationSetPending, 1);

  pending->room_name = g_strdup (room_name);
  pending->set = set;
  pending->conversation = vsx_object_ref (conversation);

  /* Listen for the changed signal so we can remove the conversation
     from the list once the game has begun */
  pending->conversation_changed_listener.notify =
    conversation_changed_cb;
  vsx_signal_add (&conversation->changed_signal,
                  &pending->conversation_changed_listener);

  vsx_list_insert (&set->pending_conversations, &pending->link);

  return conversation;
}
