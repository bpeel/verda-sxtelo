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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "config.h"

#include "vsx-proto.h"

#include <glib.h>
#include <stdarg.h>
#include <assert.h>

#define VSX_PROTO_TYPE(enum_name, type_name, ap_type_name)      \
  case enum_name:                                               \
  payload_length += sizeof (type_name);                         \
  va_arg (ap, ap_type_name);                                    \
  break;

static size_t
get_payload_length (va_list ap)
{
  /* The payload always at least includes the message ID */
  size_t payload_length = 1;

  while (true)
    {
      switch (va_arg(ap, VsxProtoType))
        {
#include "vsx-proto-types.h"

        case VSX_PROTO_TYPE_BLOB:
          payload_length += va_arg (ap, size_t);
          va_arg(ap, const uint8_t *);
          break;

        case VSX_PROTO_TYPE_STRING:
          payload_length += strlen (va_arg (ap, const char *)) + 1;
          break;

        case VSX_PROTO_TYPE_NONE:
          return payload_length;
        }
    }
}

#undef VSX_PROTO_TYPE

size_t
vsx_proto_get_frame_header_length (size_t payload_length)
{
  size_t frame_header_length = 2;

  if (payload_length > 0xffff)
    frame_header_length += sizeof (uint64_t);
  else if (payload_length >= 126)
    frame_header_length += sizeof (uint16_t);

  return frame_header_length;
}

void
vsx_proto_write_frame_header (uint8_t *buffer,
                              size_t payload_length)
{
  /* opcode (2) (binary) with FIN bit set */
  buffer[0] = 0x82;
  /* vsx_proto_write_* stores the numbers as little-endian but
   * the frame header is big-endian so we always swap the bytes
   * to make the equivalent of big-endian. Using VSX_*_TO_BE
   * won’t work, we always want to swap to compensate for the
   * conversion to LE.
   */
  if (payload_length > 0xffff)
    {
      buffer[1] = 127;
      vsx_proto_write_uint64_t (buffer + 2,
                               GUINT64_SWAP_LE_BE (payload_length));
    }
  else if (payload_length >= 126)
    {
      buffer[1] = 126;
      vsx_proto_write_uint16_t(buffer + 2,
                              GUINT16_SWAP_LE_BE (payload_length));
    }
  else
    {
      buffer[1] = payload_length;
    }
}

#define VSX_PROTO_TYPE(enum_name, type_name, ap_type_name)      \
  case enum_name:                                               \
  vsx_proto_write_ ## type_name (buffer + pos,                  \
                                 va_arg(ap, ap_type_name));     \
  pos += sizeof (type_name);                                    \
                                                                \
  break;

int
vsx_proto_write_command_v (uint8_t *buffer,
                           size_t buffer_length,
                           uint8_t command,
                           va_list ap)
{
  int pos;
  size_t payload_length = 0;
  size_t frame_header_length;
  size_t blob_length;
  const uint8_t *blob_data;
  const char *str;
  va_list ap_copy;

  va_copy (ap_copy, ap);
  payload_length = get_payload_length (ap_copy);
  va_end (ap_copy);

  frame_header_length = vsx_proto_get_frame_header_length (payload_length);

  if (frame_header_length + payload_length > buffer_length)
    return -1;

  vsx_proto_write_frame_header (buffer, payload_length);

  buffer[frame_header_length] = command;

  pos = frame_header_length + 1;

  while (true)
    {
      switch (va_arg(ap, VsxProtoType))
        {
#include "vsx-proto-types.h"

        case VSX_PROTO_TYPE_BLOB:
          blob_length = va_arg (ap, size_t);
          blob_data = va_arg (ap, const uint8_t *);
          memcpy (buffer + pos, blob_data, blob_length);
          pos += blob_length;
          break;

        case VSX_PROTO_TYPE_STRING:
          str = va_arg (ap, const char *);
          blob_length = strlen (str) + 1;
          memcpy (buffer + pos, str, blob_length);
          pos += blob_length;
          break;

        case VSX_PROTO_TYPE_NONE:
          goto done;
        }
    }
 done:

  va_end (ap);

  assert (pos == frame_header_length + payload_length);

  return pos;
}

#undef VSX_PROTO_TYPE

int
vsx_proto_write_command (uint8_t *buffer,
                         size_t buffer_length,
                         uint8_t command,
                         ...)
{
  int ret;
  va_list ap;

  va_start (ap, command);

  ret = vsx_proto_write_command_v (buffer, buffer_length, command, ap);

  va_end(ap);

  return ret;
}

#define VSX_PROTO_TYPE(enum_name, type_name, ap_type_name)      \
  case enum_name:                                               \
  if ((size_t) pos + sizeof (type_name) > length) {             \
    ret = false;                                                \
    goto done;                                                  \
  }                                                             \
                                                                \
        {                                                       \
          type_name *val = va_arg(ap, type_name *);             \
          *val = vsx_proto_read_ ## type_name(buffer + pos);    \
        }                                                       \
                                                                \
        pos += sizeof (type_name);                              \
                                                                \
        break;

bool
vsx_proto_read_payload (const uint8_t *buffer,
                        size_t length,
                        ...)
{
  size_t pos = 0;
  bool ret = true;
  va_list ap;
  const uint8_t **blob_data;
  size_t *blob_size;
  const char **str;
  const uint8_t *str_end;

  va_start (ap, length);

  while (true)
    {
      switch (va_arg (ap, VsxProtoType))
        {
#include "vsx-proto-types.h"

        case VSX_PROTO_TYPE_BLOB:
          blob_size = va_arg (ap, size_t *);
          blob_data = va_arg (ap, const uint8_t **);
          *blob_size = length - pos;
          *blob_data = buffer + pos;
          pos = length;
          break;

        case VSX_PROTO_TYPE_STRING:
          str = va_arg (ap, const char **);
          str_end = memchr (buffer + pos, '\0', length - pos);
          if (str_end == NULL)
            {
              ret = false;
              goto done;
            }
          *str = (const char *) buffer + pos;
          if (!g_utf8_validate (*str, -1, NULL))
            {
              ret = false;
              goto done;
            }
          pos = str_end - buffer + 1;
          break;

        case VSX_PROTO_TYPE_NONE:
          if (pos != length)
            ret = false;
          goto done;
        }
    }

 done:
  va_end (ap);

  return ret;
}

#undef VSX_PROTO_TYPE
