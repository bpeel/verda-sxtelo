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

#include "gml-conversation.h"
#include "gml-marshal.h"
#include "gml-main-context.h"

G_DEFINE_TYPE (GmlConversation, gml_conversation, G_TYPE_OBJECT);

enum
{
  CHANGED_SIGNAL,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

/* Time in microseconds after the last message has been added to the
   conversation before it is considered not in use */
#define GML_CONVERSATION_STALE_TIME (60 * 5 * (gint64) 1000000)

static void
gml_conversation_finalize (GObject *object)
{
  GmlConversation *self = (GmlConversation *) object;
  int i;

  for (i = 0; i < self->messages->len; i++)
    {
      GmlConversationMessage *message = &g_array_index (self->messages,
                                                        GmlConversationMessage,
                                                        i);
      g_free (message->text);
    }

  g_array_free (self->messages, TRUE);

  G_OBJECT_CLASS (gml_conversation_parent_class)->finalize (object);
}

static void
gml_conversation_class_init (GmlConversationClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = gml_conversation_finalize;

  signals[CHANGED_SIGNAL] =
    g_signal_new ("changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_FIRST,
                  0, /* no class method */
                  NULL, /* accumulator */
                  NULL, /* accu_data */
                  gml_marshal_VOID__VOID,
                  G_TYPE_NONE, /* return type */
                  0 /* num arguments */);
}

static void
gml_conversation_init (GmlConversation *self)
{
  self->messages = g_array_new (FALSE, FALSE, sizeof (GmlConversationMessage));

  self->state = GML_CONVERSATION_AWAITING_PARTNER;

  self->stale_age = gml_main_context_get_monotonic_clock (NULL);
}

static void
gml_conversation_changed (GmlConversation *conversation)
{
  g_signal_emit (conversation,
                 signals[CHANGED_SIGNAL],
                 0 /* detail */);
}

void
gml_conversation_start (GmlConversation *conversation)
{
  if (conversation->state == GML_CONVERSATION_AWAITING_PARTNER)
    {
      conversation->state = GML_CONVERSATION_IN_PROGRESS;
      gml_conversation_changed (conversation);
    }
}

void
gml_conversation_finish (GmlConversation *conversation)
{
  conversation->state = GML_CONVERSATION_FINISHED;
  gml_conversation_changed (conversation);
}

void
gml_conversation_check_stale (GmlConversation *conversation)
{
  if (gml_main_context_get_monotonic_clock (NULL) - conversation->stale_age
      >= GML_CONVERSATION_STALE_TIME)
    gml_conversation_finish (conversation);
}

void
gml_conversation_add_message (GmlConversation *conversation,
                              unsigned int person_num,
                              const char *buffer,
                              unsigned int length)

{
  GmlConversationMessage *message;
  GString *message_str;

  /* Ignore attempts to add messages to a conversation that has not
     yet started */
  if (conversation->state != GML_CONVERSATION_IN_PROGRESS)
    return;

  g_array_set_size (conversation->messages,
                    conversation->messages->len + 1);
  message = &g_array_index (conversation->messages,
                            GmlConversationMessage,
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

  conversation->stale_age = gml_main_context_get_monotonic_clock (NULL);

  gml_conversation_changed (conversation);
}

GmlConversation *
gml_conversation_new (void)
{
  GmlConversation *self = g_object_new (GML_TYPE_CONVERSATION, NULL);

  return self;
}
