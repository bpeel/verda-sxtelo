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

#include "vsx-person-set.h"
#include "vsx-conversation-set.h"
#include "vsx-signal.h"
#include "vsx-error.h"
#include "vsx-netaddress.h"

typedef struct _VsxConnection VsxConnection;

typedef enum
{
  VSX_CONNECTION_ERROR_INVALID_PROTOCOL,
} VsxConnectionError;

extern struct vsx_error_domain
vsx_connection_error;

VsxConnection *
vsx_connection_new (const struct vsx_netaddress *socket_address,
                    VsxConversationSet *conversation_set,
                    VsxPersonSet *person_set);

size_t
vsx_connection_fill_output_buffer (VsxConnection *conn,
                                   uint8_t *buffer,
                                   size_t buffer_size);

bool
vsx_connection_parse_data (VsxConnection *conn,
                           const uint8_t *buffer,
                           size_t buffer_length,
                           struct vsx_error **error);

bool
vsx_connection_parse_eof (VsxConnection *conn,
                          struct vsx_error **error);

bool
vsx_connection_is_finished (VsxConnection *conn);

bool
vsx_connection_has_data (VsxConnection *conn);

VsxSignal *
vsx_connection_get_changed_signal (VsxConnection *conn);

int64_t
vsx_connection_get_last_message_time (VsxConnection *conn);

void
vsx_connection_free (VsxConnection *conn);

#endif /* __VSX_CONNECTION_H__ */
