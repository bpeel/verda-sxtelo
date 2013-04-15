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

#include "vsx-conversation.h"
#include "vsx-main-context.h"
#include "vsx-log.h"

static void
vsx_conversation_free (void *object)
{
  VsxConversation *self = object;
  int i;

  for (i = 0; i < self->messages->len; i++)
    {
      VsxConversationMessage *message = &g_array_index (self->messages,
                                                        VsxConversationMessage,
                                                        i);
      g_free (message->text);
    }

  for (i = 0; i < self->n_players; i++)
    vsx_player_free (self->players[i]);

  g_array_free (self->messages, TRUE);

  vsx_object_get_class ()->free (object);
}

static const VsxObjectClass *
vsx_conversation_get_class (void)
{
  static VsxObjectClass klass;

  if (klass.free == NULL)
    {
      klass = *vsx_object_get_class ();
      klass.instance_size = sizeof (VsxConversation);
      klass.free = vsx_conversation_free;
    }

  return &klass;
}

static void
vsx_conversation_changed (VsxConversation *conversation)
{
  vsx_signal_emit (&conversation->changed_signal, conversation);
}

static void
vsx_conversation_player_changed (VsxConversation *conversation,
                                 VsxPlayer *player)
{
  vsx_signal_emit (&conversation->player_changed_signal, player);
}

void
vsx_conversation_start (VsxConversation *conversation)
{
  if (conversation->state == VSX_CONVERSATION_AWAITING_START)
    {
      conversation->state = VSX_CONVERSATION_IN_PROGRESS;
      vsx_conversation_changed (conversation);
    }
}

void
vsx_conversation_finish (VsxConversation *conversation)
{
  if (conversation->state != VSX_CONVERSATION_FINISHED)
    {
      vsx_log ("Conversation finished");
      conversation->state = VSX_CONVERSATION_FINISHED;
      vsx_conversation_changed (conversation);
    }
}

void
vsx_conversation_add_message (VsxConversation *conversation,
                              unsigned int person_num,
                              const char *buffer,
                              unsigned int length)
{
  VsxConversationMessage *message;
  GString *message_str;

  /* Ignore attempts to add messages to a conversation that has finished */
  if (conversation->state == VSX_CONVERSATION_FINISHED)
    return;

  g_array_set_size (conversation->messages,
                    conversation->messages->len + 1);
  message = &g_array_index (conversation->messages,
                            VsxConversationMessage,
                            conversation->messages->len - 1);

  message_str = g_string_sized_new (length + 32);

  g_string_append_printf (message_str,
                          "[\"message\", {\"person\": %u, "
                          "\"text\": \"",
                          person_num);
  while (length-- > 0)
    {
      /* Replace any control characters or spaces with a space */
      if ((guint8) *buffer <= ' ')
        g_string_append_c (message_str, ' ');
      /* Quote special characters */
      else if (*buffer == '"' || *buffer == '\\')
        {
          g_string_append_c (message_str, '\\');
          g_string_append_c (message_str, *buffer);
        }
      else
        g_string_append_c (message_str, *buffer);

      buffer++;
    }
  g_string_append (message_str, "\"}]\r\n");

  message->length = message_str->len;
  message->text = g_string_free (message_str, FALSE);

  vsx_conversation_changed (conversation);
}

void
vsx_conversation_set_typing (VsxConversation *conversation,
                             unsigned int person_num,
                             gboolean typing)
{
  unsigned int new_mask = conversation->typing_mask;

  if (typing)
    new_mask |= 1 << person_num;
  else
    new_mask &= ~(1 << person_num);

  if (new_mask != conversation->typing_mask)
    {
      conversation->typing_mask = new_mask;
      vsx_conversation_changed (conversation);
    }
}

VsxPlayer *
vsx_conversation_add_player (VsxConversation *conversation,
                             const char *player_name)
{
  VsxPlayer *player;

  g_assert_cmpint (conversation->n_players, <, VSX_CONVERSATION_MAX_PLAYERS);

  player = vsx_player_new (player_name, conversation->n_players);
  conversation->players[conversation->n_players++] = player;

  vsx_conversation_player_changed (conversation, player);

  return player;
}

VsxConversation *
vsx_conversation_new (void)
{
  VsxConversation *self = vsx_object_allocate (vsx_conversation_get_class ());

  vsx_object_init (self);

  vsx_signal_init (&self->changed_signal);
  vsx_signal_init (&self->player_changed_signal);

  self->messages = g_array_new (FALSE, FALSE, sizeof (VsxConversationMessage));

  self->state = VSX_CONVERSATION_AWAITING_START;

  return self;
}
