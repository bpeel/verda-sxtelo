/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2015, 2020, 2021  Neil Roberts
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

#ifndef __VSX_PROTO_H__
#define __VSX_PROTO_H__

#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

/* Maximum number of bytes allowed in a payload. The server keeps a
 * buffer of this size around for each connection, so we don’t want it
 * to be too large.
 */
#define VSX_PROTO_MAX_PAYLOAD_SIZE 1024

/* The WebSocket protocol says that a control frame payload can not be
 * longer than 125 bytes.
 */
#define VSX_PROTO_MAX_CONTROL_FRAME_PAYLOAD 125

#define VSX_PROTO_MAX_FRAME_HEADER_LENGTH (1 + 1 + 8 + 4)

#define VSX_PROTO_NEW_PLAYER 0x80

#define VSX_PROTO_PLAYER_NAME 0x00
#define VSX_PROTO_MESSAGE 0x01

typedef enum
  {
    VSX_PROTO_TYPE_UINT8,
    VSX_PROTO_TYPE_UINT16,
    VSX_PROTO_TYPE_UINT32,
    VSX_PROTO_TYPE_UINT64,
    VSX_PROTO_TYPE_BLOB,
    VSX_PROTO_TYPE_STRING,
    VSX_PROTO_TYPE_NONE
  } VsxProtoType;

static inline void
vsx_proto_write_guint8 (guint8 *buffer,
                        guint8 value)
{
  *buffer = value;
}

static inline void
vsx_proto_write_guint16 (guint8 *buffer,
                         guint16 value)
{
  value = GUINT16_TO_LE (value);
  memcpy (buffer, &value, sizeof value);
}

static inline void
vsx_proto_write_guint32 (guint8 *buffer,
                         guint32 value)
{
  value = GUINT32_TO_LE (value);
  memcpy (buffer, &value, sizeof value);
}

static inline void
vsx_proto_write_guint64 (guint8 *buffer,
                         guint64 value)
{
  value = GUINT64_TO_LE (value);
  memcpy (buffer, &value, sizeof value);
}

int
vsx_proto_write_command_v (guint8 *buffer,
                           size_t buffer_length,
                           guint8 command,
                           va_list ap);

int
vsx_proto_write_command (guint8 *buffer,
                         size_t buffer_length,
                         guint8 command,
                         ...);

static inline guint8
vsx_proto_read_guint8 (const guint8 *buffer)
{
  return *buffer;
}

static inline guint16
vsx_proto_read_guint16 (const guint8 *buffer)
{
  guint16 value;
  memcpy (&value, buffer, sizeof value);
  return GUINT16_FROM_LE (value);
}

static inline guint32
vsx_proto_read_guint32 (const guint8 *buffer)
{
  guint32 value;
  memcpy (&value, buffer, sizeof value);
  return GUINT32_FROM_LE (value);
}

static inline guint64
vsx_proto_read_guint64 (const guint8 *buffer)
{
  guint64 value;
  memcpy (&value, buffer, sizeof value);
  return GUINT64_FROM_LE (value);
}

gboolean
vsx_proto_read_payload (const guint8 *buffer,
                        size_t length,
                        ...);

size_t
vsx_proto_get_frame_header_length (size_t payload_length);

void
vsx_proto_write_frame_header (guint8 *buffer,
                              size_t payload_length);

#endif /* __VSX_PROTO_H__ */
