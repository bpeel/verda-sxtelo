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

static void gml_conversation_dispose (GObject *object);
static void gml_conversation_finalize (GObject *object);

G_DEFINE_TYPE (GmlConversation, gml_conversation, G_TYPE_OBJECT);

static void
gml_conversation_class_init (GmlConversationClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->dispose = gml_conversation_dispose;
  gobject_class->finalize = gml_conversation_finalize;
}

static void
gml_conversation_init (GmlConversation *self)
{
}

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

  G_OBJECT_CLASS (gml_conversation_parent_class)->finalize (object);
}

GmlConversation *
gml_conversation_new (void)
{
  GmlConversation *self = g_object_new (GML_TYPE_CONVERSATION, NULL);

  return self;
}
