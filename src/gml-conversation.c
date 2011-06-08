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
#include "gml-buffer-usage.h"
#include "gml-marshal.h"

G_DEFINE_TYPE (GmlConversation, gml_conversation, G_TYPE_OBJECT);

enum
{
  CHANGED_SIGNAL,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

static void
gml_conversation_dispose (GObject *object)
{
  GmlConversation *self = (GmlConversation *) object;

  G_OBJECT_CLASS (gml_conversation_parent_class)->dispose (object);
}

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

  object_class->dispose = gml_conversation_dispose;
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
}

void
gml_conversation_add_message (GmlConversation *conversation,
                              GmlBufferUsage buffer_usage,
                              unsigned int length,
                              const char *buffer)
{
  GmlConversationMessage *message;

  g_array_set_size (conversation->messages,
                    conversation->messages->len + 1);
  message = &g_array_index (conversation->messages,
                            GmlConversationMessage,
                            conversation->messages->len - 1);

  switch (buffer_usage)
    {
    case GML_BUFFER_USAGE_COPY:
      message->text = g_strdup (buffer);
      break;

    case GML_BUFFER_USAGE_TAKE:
      message->text = (char *) buffer;
      break;
    }

  message->length = length;

  g_signal_emit (conversation,
                 signals[CHANGED_SIGNAL],
                 0 /* detail */);
}

GmlConversation *
gml_conversation_new (void)
{
  GmlConversation *self = g_object_new (GML_TYPE_CONVERSATION, NULL);

  return self;
}
