/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2013, 2014, 2019, 2021  Neil Roberts
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "vsx-key-value.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdarg.h>

typedef enum
{
  VSX_KEY_VALUE_STATE_HEADER_START,
  VSX_KEY_VALUE_STATE_BAD_HEADER_START,
  VSX_KEY_VALUE_STATE_READING_HEADER,
  VSX_KEY_VALUE_STATE_END_HEADER_LINE,
  VSX_KEY_VALUE_STATE_END_HEADER_LINE2,
  VSX_KEY_VALUE_STATE_FIELD_START,
  VSX_KEY_VALUE_STATE_READING_FIELD_NAME,
  VSX_KEY_VALUE_STATE_WAITING_EQUALS,
  VSX_KEY_VALUE_STATE_BAD_FIELD,
  VSX_KEY_VALUE_STATE_WAITING_VALUE_START,
  VSX_KEY_VALUE_STATE_READING_VALUE,
} VsxKeyValueState;

typedef struct
{
  VsxKeyValueState state;

  struct vsx_ecc *ecc;

  VsxKeyValueCallback func;
  VsxKeyValueErrorCallback error_func;
  void *user_data;

  GString *key_buffer;
  GString *value_buffer;
  GString *error_buffer;

  int line_num;
} VsxKeyValueData;

G_GNUC_PRINTF (2, 3)
static void
log_error (VsxKeyValueData *data, const char *format, ...)
{
  g_string_truncate (data->error_buffer, 0);

  va_list ap;

  va_start (ap, format);
  g_string_append_vprintf (data->error_buffer, format, ap);
  va_end (ap);

  data->error_func (data->error_buffer->str, data->user_data);
}

static void
process_header (VsxKeyValueData *data)
{
  data->func (VSX_KEY_VALUE_EVENT_HEADER, data->line_num, NULL, /* key */
              data->value_buffer->str, data->user_data);
}

static void
process_value (VsxKeyValueData *data)
{
  size_t value_length = data->value_buffer->len;

  while (value_length > 0 && data->value_buffer->str[value_length - 1] == ' ')
    value_length--;

  g_string_truncate (data->value_buffer, value_length);

  data->func (VSX_KEY_VALUE_EVENT_PROPERTY,
              data->line_num,
              data->key_buffer->str,
              data->value_buffer->str,
              data->user_data);
}

static void
handle_byte (VsxKeyValueData *data, int ch)
{
  switch (data->state)
    {
    case VSX_KEY_VALUE_STATE_HEADER_START:
      if (ch == '[')
        {
          data->state = VSX_KEY_VALUE_STATE_READING_HEADER;
          g_string_truncate (data->value_buffer, 0);
        }
      else if (ch != ' ' && ch != '\n')
        {
          log_error (data, "Invalid header on line %i", data->line_num);
          data->state = VSX_KEY_VALUE_STATE_BAD_HEADER_START;
        }
      return;
    case VSX_KEY_VALUE_STATE_BAD_HEADER_START:
      if (ch == '\n')
        data->state = VSX_KEY_VALUE_STATE_HEADER_START;
      return;
    case VSX_KEY_VALUE_STATE_READING_HEADER:
      if (ch == '\n')
        {
          log_error (data, "Invalid header on line %i", data->line_num);
          data->state = VSX_KEY_VALUE_STATE_FIELD_START;
        }
      else if (ch == ']')
        {
          process_header (data);
          data->state = VSX_KEY_VALUE_STATE_END_HEADER_LINE;
        }
      else
        {
          g_string_append_c (data->value_buffer, ch);
        }
      return;
    case VSX_KEY_VALUE_STATE_END_HEADER_LINE:
      if (ch == '\n')
        {
          data->state = VSX_KEY_VALUE_STATE_FIELD_START;
        }
      else if (ch != ' ')
        {
          log_error (data, "Junk after header on line %i", data->line_num);
          data->state = VSX_KEY_VALUE_STATE_END_HEADER_LINE2;
        }
      return;
    case VSX_KEY_VALUE_STATE_END_HEADER_LINE2:
      if (ch == '\n')
        data->state = VSX_KEY_VALUE_STATE_FIELD_START;
      return;
    case VSX_KEY_VALUE_STATE_FIELD_START:
      if (ch == '[')
        {
          data->state = VSX_KEY_VALUE_STATE_READING_HEADER;
          g_string_truncate (data->value_buffer, 0);
        }
      else if (ch != ' ' && ch != '\n')
        {
          g_string_truncate (data->key_buffer, 0);
          g_string_append_c (data->key_buffer, ch);
          data->state = VSX_KEY_VALUE_STATE_READING_FIELD_NAME;
        }
      return;
    case VSX_KEY_VALUE_STATE_READING_FIELD_NAME:
      if (ch == ' ')
        {
          data->state = VSX_KEY_VALUE_STATE_WAITING_EQUALS;
        }
      else if (ch == '=')
        {
          data->state = VSX_KEY_VALUE_STATE_WAITING_VALUE_START;
        }
      else if (ch == '\n')
        {
          log_error (data, "Invalid line %i", data->line_num);
          data->state = VSX_KEY_VALUE_STATE_FIELD_START;
        }
      else
        {
          g_string_append_c (data->key_buffer, ch);
        }
      return;
    case VSX_KEY_VALUE_STATE_WAITING_EQUALS:
      if (ch == '=')
        {
          data->state = VSX_KEY_VALUE_STATE_WAITING_VALUE_START;
        }
      else if (ch == '\n')
        {
          log_error (data, "Invalid line %i", data->line_num);
          data->state = VSX_KEY_VALUE_STATE_FIELD_START;
        }
      else if (ch != ' ')
        {
          log_error (data, "Invalid line %i", data->line_num);
          data->state = VSX_KEY_VALUE_STATE_BAD_FIELD;
        }
      return;
    case VSX_KEY_VALUE_STATE_WAITING_VALUE_START:
      if (ch == '\n')
        {
          g_string_truncate (data->value_buffer, 0);
          process_value (data);
          data->state = VSX_KEY_VALUE_STATE_FIELD_START;
        }
      else if (ch != ' ')
        {
          g_string_truncate (data->value_buffer, 0);
          g_string_append_c (data->value_buffer, ch);
          data->state = VSX_KEY_VALUE_STATE_READING_VALUE;
        }
      return;
    case VSX_KEY_VALUE_STATE_READING_VALUE:
      if (ch == '\n')
        {
          process_value (data);
          data->state = VSX_KEY_VALUE_STATE_FIELD_START;
        }
      else
        {
          g_string_append_c (data->value_buffer, ch);
        }
      return;
    case VSX_KEY_VALUE_STATE_BAD_FIELD:
      if (ch == '\n')
        data->state = VSX_KEY_VALUE_STATE_FIELD_START;
      return;
    }

  g_critical ("Invalid state reached");
}

void
vsx_key_value_load (FILE *file,
                    VsxKeyValueCallback func,
                    VsxKeyValueErrorCallback error_func,
                    void *user_data)
{
  VsxKeyValueData data;
  int ch;

  data.line_num = 1;
  data.state = VSX_KEY_VALUE_STATE_HEADER_START;

  data.key_buffer = g_string_new (NULL);
  data.value_buffer = g_string_new (NULL);
  data.error_buffer = g_string_new (NULL);

  data.func = func;
  data.error_func = error_func;
  data.user_data = user_data;

  while ((ch = fgetc (file)) != EOF)
    {
      handle_byte (&data, ch);

      if (ch == '\n')
        data.line_num++;
    }

  handle_byte (&data, '\n');

  g_string_free (data.key_buffer, TRUE);
  g_string_free (data.value_buffer, TRUE);
  g_string_free (data.error_buffer, TRUE);
}

bool
vsx_key_value_parse_bool_value (int line_number,
                                const char *value,
                                bool *result)
{
  if (!strcmp (value, "true"))
    {
      *result = true;
      return true;
    }

  if (!strcmp (value, "false"))
    {
      *result = false;
      return true;
    }

  return false;
}

bool
vsx_key_value_parse_int_value (int line_number,
                               const char *value,
                               int64_t max,
                               int64_t *result)
{
  long long int int_value;
  char *tail;

  errno = 0;

  int_value = strtoll (value, &tail, 10);

  if (errno || tail == value || *tail || value < 0)
    {
      return false;
    }

  if (int_value > max)
    {
      return false;
    }

  *result = int_value;

  return true;
}
