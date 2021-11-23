/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2013, 2015, 2020, 2021  Neil Roberts
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

#ifndef __VSX_CONNECTION_H__
#define __VSX_CONNECTION_H__

#include <glib.h>
#include <gio/gio.h>

#include "vsx-person-set.h"
#include "vsx-conversation-set.h"

typedef struct _VsxConnection VsxConnection;

VsxConnection *
vsx_connection_new (GSocketAddress *socket_address,
                    VsxConversationSet *conversation_set,
                    VsxPersonSet *person_set);

size_t
vsx_connection_fill_output_buffer (VsxConnection *conn,
                                   guint8 *buffer,
                                   size_t buffer_size);

gboolean
vsx_connection_parse_data (VsxConnection *conn,
                           const guint8 *buffer,
                           size_t buffer_length,
                           GError **error);

gboolean
vsx_connection_parse_eof (VsxConnection *conn,
                          GError **error);


void
vsx_connection_free (VsxConnection *conn);

#endif /* __VSX_CONNECTION_H__ */
