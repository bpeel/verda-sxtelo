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

#include "vsx-conversation.h"
#include "vsx-marshal.h"
#include "vsx-main-context.h"
#include "vsx-log.h"

G_DEFINE_TYPE (VsxConversation, vsx_conversation, G_TYPE_OBJECT);

enum
{
  CHANGED_SIGNAL,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

static void
vsx_conversation_finalize (GObject *object)
{
  VsxConversation *self = (VsxConversation *) object;
  int i;

  for (i = 0; i < self->messages->len; i++)
    {
      VsxConversationMessage *message = &g_array_index (self->messages,
                                                        VsxConversationMessage,
                                                        i);
      g_free (message->text);
    }

  g_array_free (self->messages, TRUE);

  G_OBJECT_CLASS (vsx_conversation_parent_class)->finalize (object);
}

static void
vsx_conversation_class_init (VsxConversationClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = vsx_conversation_finalize;

  signals[CHANGED_SIGNAL] =
    g_signal_new ("changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_FIRST,
                  0, /* no class method */
                  NULL, /* accumulator */
                  NULL, /* accu_data */
                  vsx_marshal_VOID__VOID,
                  G_TYPE_NONE, /* return type */
                  0 /* num arguments */);
}

static void
vsx_conversation_init (VsxConversation *self)
{
  self->messages = g_array_new (FALSE, FALSE, sizeof (VsxConversationMessage));

  self->state = VSX_CONVERSATION_AWAITING_PARTNER;
}

static void
vsx_conversation_changed (VsxConversation *conversation)
{
  g_signal_emit (conversation,
                 signals[CHANGED_SIGNAL],
                 0 /* detail */);
}

void
vsx_conversation_start (VsxConversation *conversation)
{
  if (conversation->state == VSX_CONVERSATION_AWAITING_PARTNER)
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

  /* Ignore attempts to add messages to a conversation that has not
     yet started */
  if (conversation->state != VSX_CONVERSATION_IN_PROGRESS)
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

VsxConversation *
vsx_conversation_new (void)
{
  VsxConversation *self = g_object_new (VSX_TYPE_CONVERSATION, NULL);

  return self;
}
