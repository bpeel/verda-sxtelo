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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "vsx-connection.h"

/* VsxConnection specifically handles connections using the WebSocket
 * protocol. Connections via the HTTP protocol use an HTTP parser
 * instead. The weird name is because eventually the HTTP part is
 * expected to be removed and VsxConnection will be the only type of
 * connection.
 */

struct _VsxConnection
{
  GSocketAddress *socket_address;
  VsxConversationSet *conversation_set;
  VsxPersonSet *person_set;
};

VsxConnection *
vsx_connection_new (GSocketAddress *socket_address,
                    VsxConversationSet *conversation_set,
                    VsxPersonSet *person_set)
{
  VsxConnection *conn = g_new0 (VsxConnection, 1);

  conn->socket_address = g_object_ref (socket_address);
  conn->conversation_set = vsx_object_ref (conversation_set);
  conn->person_set = vsx_object_ref (person_set);

  return conn;
}

size_t
vsx_connection_fill_output_buffer (VsxConnection *conn,
                                   guint8 *buffer,
                                   size_t buffer_size)
{
  return 0;
}

gboolean
vsx_connection_parse_eof (VsxConnection *conn,
                          GError **error)
{
  return TRUE;
}

gboolean
vsx_connection_parse_data (VsxConnection *conn,
                           const guint8 *buffer,
                           size_t buffer_length,
                           GError **error)
{
  return TRUE;
}

void
vsx_connection_free (VsxConnection *conn)
{
  g_object_unref (conn->socket_address);
  vsx_object_unref (conn->conversation_set);
  vsx_object_unref (conn->person_set);
  g_free (conn);
}
