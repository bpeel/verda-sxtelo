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
#include <stdint.h>

/* Maximum number of bytes allowed in a payload. The server keeps a
 * buffer of this size around for each connection, so we don’t want it
 * to be too large.
 */
#define VSX_PROTO_MAX_PAYLOAD_SIZE 1024

/* Maximum number of bytes allowed in a room or player name */
#define VSX_PROTO_MAX_NAME_LENGTH 256

/* Maxmimum number of bytes allowed in a message */
#define VSX_PROTO_MAX_MESSAGE_LENGTH 1000

/* The WebSocket protocol says that a control frame payload can not be
 * longer than 125 bytes.
 */
#define VSX_PROTO_MAX_CONTROL_FRAME_PAYLOAD 125

#define VSX_PROTO_MAX_FRAME_HEADER_LENGTH (1 + 1 + 8 + 4)

#define VSX_PROTO_NEW_PLAYER 0x80
#define VSX_PROTO_RECONNECT 0x81
#define VSX_PROTO_KEEP_ALIVE 0x83
#define VSX_PROTO_LEAVE 0x84
#define VSX_PROTO_SEND_MESSAGE 0x85
#define VSX_PROTO_START_TYPING 0x86
#define VSX_PROTO_STOP_TYPING 0x87
#define VSX_PROTO_MOVE_TILE 0x88
#define VSX_PROTO_TURN 0x89
#define VSX_PROTO_SHOUT 0x8A
#define VSX_PROTO_SET_N_TILES 0x8B

#define VSX_PROTO_PLAYER_ID 0x00
#define VSX_PROTO_MESSAGE 0x01
#define VSX_PROTO_N_TILES 0x02
#define VSX_PROTO_TILE 0x03
#define VSX_PROTO_PLAYER_NAME 0x04
#define VSX_PROTO_PLAYER 0x05
#define VSX_PROTO_PLAYER_SHOUTED 0x06
#define VSX_PROTO_SYNC 0x07
#define VSX_PROTO_END 0x08

typedef enum
  {
    VSX_PROTO_TYPE_UINT8,
    VSX_PROTO_TYPE_UINT16,
    VSX_PROTO_TYPE_UINT32,
    VSX_PROTO_TYPE_UINT64,
    VSX_PROTO_TYPE_INT16,
    VSX_PROTO_TYPE_BLOB,
    VSX_PROTO_TYPE_STRING,
    VSX_PROTO_TYPE_NONE
  } VsxProtoType;

static inline void
vsx_proto_write_uint8_t (uint8_t *buffer,
                         uint8_t value)
{
  *buffer = value;
}

static inline void
vsx_proto_write_uint16_t (uint8_t *buffer,
                          uint16_t value)
{
  value = GUINT16_TO_LE (value);
  memcpy (buffer, &value, sizeof value);
}

static inline void
vsx_proto_write_uint32_t (uint8_t *buffer,
                          uint32_t value)
{
  value = GUINT32_TO_LE (value);
  memcpy (buffer, &value, sizeof value);
}

static inline void
vsx_proto_write_uint64_t (uint8_t *buffer,
                          uint64_t value)
{
  value = GUINT64_TO_LE (value);
  memcpy (buffer, &value, sizeof value);
}

static inline void
vsx_proto_write_int16_t (uint8_t *buffer,
                         int16_t value)
{
  value = GINT16_TO_LE (value);
  memcpy (buffer, &value, sizeof value);
}

int
vsx_proto_write_command_v (uint8_t *buffer,
                           size_t buffer_length,
                           uint8_t command,
                           va_list ap);

int
vsx_proto_write_command (uint8_t *buffer,
                         size_t buffer_length,
                         uint8_t command,
                         ...);

static inline uint8_t
vsx_proto_read_uint8_t (const uint8_t *buffer)
{
  return *buffer;
}

static inline uint16_t
vsx_proto_read_uint16_t (const uint8_t *buffer)
{
  uint16_t value;
  memcpy (&value, buffer, sizeof value);
  return GUINT16_FROM_LE (value);
}

static inline uint32_t
vsx_proto_read_uint32_t (const uint8_t *buffer)
{
  uint32_t value;
  memcpy (&value, buffer, sizeof value);
  return GUINT32_FROM_LE (value);
}

static inline uint64_t
vsx_proto_read_uint64_t (const uint8_t *buffer)
{
  uint64_t value;
  memcpy (&value, buffer, sizeof value);
  return GUINT64_FROM_LE (value);
}

static inline int16_t
vsx_proto_read_int16_t (const uint8_t *buffer)
{
  int16_t value;
  memcpy (&value, buffer, sizeof value);
  return GINT16_FROM_LE (value);
}

gboolean
vsx_proto_read_payload (const uint8_t *buffer,
                        size_t length,
                        ...);

size_t
vsx_proto_get_frame_header_length (size_t payload_length);

void
vsx_proto_write_frame_header (uint8_t *buffer,
                              size_t payload_length);

#endif /* __VSX_PROTO_H__ */
