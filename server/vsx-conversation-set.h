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

#ifndef VSX_CONVERSATION_SET_H
#define VSX_CONVERSATION_SET_H

#include "vsx-conversation.h"
#include "vsx-netaddress.h"

/* This class represents a list of pending conversations. It only
   contains conversations that can still be joined. As soon as the
   game starts or everyone leaves the conversation then it is removed
   from the list */

typedef struct _VsxConversationSet VsxConversationSet;

VsxConversationSet *
vsx_conversation_set_new (void);

VsxConversation *
vsx_conversation_set_get_conversation (VsxConversationSet *set,
                                       VsxConversationId id);

VsxConversation *
vsx_conversation_set_generate_conversation (VsxConversationSet *set,
                                            const char *language_code,
                                            const struct vsx_netaddress *addr);

VsxConversation *
vsx_conversation_set_get_pending_conversation (VsxConversationSet *set,
                                               const char *room_name,
                                               const struct vsx_netaddress *a);

#endif /* VSX_CONVERSATION_SET_H */
