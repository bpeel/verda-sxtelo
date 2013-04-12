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

#ifndef __VSX_CONVERSATION_SET_H__
#define __VSX_CONVERSATION_SET_H__

#include <glib-object.h>
#include <gio/gio.h>
#include "vsx-conversation.h"

/* This class represents a hash table of pending conversations. It
   only contains conversations that only have one person. As soon as
   the second person joins the conversation is removed from the hash
   table */

G_BEGIN_DECLS

#define VSX_TYPE_CONVERSATION_SET               \
  (vsx_conversation_set_get_type())
#define VSX_CONVERSATION_SET(obj)                               \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                           \
                               VSX_TYPE_CONVERSATION_SET,       \
                               VsxConversationSet))
#define VSX_CONVERSATION_SET_CLASS(klass)               \
  (G_TYPE_CHECK_CLASS_CAST ((klass),                    \
                            VSX_TYPE_CONVERSATION_SET,  \
                            VsxConversationSetClass))
#define VSX_IS_CONVERSATION_SET(obj)                            \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                           \
                               VSX_TYPE_CONVERSATION_SET))
#define VSX_IS_CONVERSATION_SET_CLASS(klass)            \
  (G_TYPE_CHECK_CLASS_TYPE ((klass),                    \
                            VSX_TYPE_CONVERSATION_SET))
#define VSX_CONVERSATION_SET_GET_CLASS(obj)             \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                    \
                              VSX_CONVERSATION_SET,     \
                              VsxConversationSetClass))

typedef struct _VsxConversationSet      VsxConversationSet;
typedef struct _VsxConversationSetClass VsxConversationSetClass;

struct _VsxConversationSetClass
{
  GObjectClass parent_class;
};

struct _VsxConversationSet
{
  GObject parent;

  /* Hash table of pending conversations. This only contains
     conversations that only have one person. The key is the name of
     the room and the value is the a VsxServerConversationHashData
     struct (which contains a pointer to the conversation). The hash
     table listens for the changed signal on the conversation so that
     it can remove the conversation if the first person leaves before
     the second person joins. */
  GHashTable *hash_table;
};

GType
vsx_conversation_set_get_type (void) G_GNUC_CONST;

VsxConversationSet *
vsx_conversation_set_new (void);

VsxConversation *
vsx_conversation_set_get_conversation (VsxConversationSet *set,
                                       const char *room_name);

G_END_DECLS

#endif /* __VSX_CONVERSATION_SET_H__ */
