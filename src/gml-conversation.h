/*
 * Gemelo - A person for chatting with strangers in a foreign language
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

#ifndef __GML_CONVERSATION_H__
#define __GML_CONVERSATION_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define GML_TYPE_CONVERSATION                                           \
  (gml_conversation_get_type())
#define GML_CONVERSATION(obj)                                           \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                                   \
                               GML_TYPE_CONVERSATION,                   \
                               GmlConversation))
#define GML_CONVERSATION_CLASS(klass)                                   \
  (G_TYPE_CHECK_CLASS_CAST ((klass),                                    \
                            GML_TYPE_CONVERSATION,                      \
                            GmlConversationClass))
#define GML_IS_CONVERSATION(obj)                                        \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                                   \
                               GML_TYPE_CONVERSATION))
#define GML_IS_CONVERSATION_CLASS(klass)                                \
  (G_TYPE_CHECK_CLASS_TYPE ((klass),                                    \
                            GML_TYPE_CONVERSATION))
#define GML_CONVERSATION_GET_CLASS(obj)                                 \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                                    \
                              GML_CONVERSATION,                         \
                              GmlConversationClass))

typedef struct _GmlConversation      GmlConversation;
typedef struct _GmlConversationClass GmlConversationClass;

struct _GmlConversationClass
{
  GObjectClass parent_class;
};

struct _GmlConversation
{
  GObject parent;
};

GType
gml_conversation_get_type (void) G_GNUC_CONST;

GmlConversation *
gml_conversation_new (void);

G_END_DECLS

#endif /* __GML_CONVERSATION_H__ */
