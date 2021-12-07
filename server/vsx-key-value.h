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

#ifndef VSX_KEY_VALUE_H
#define VSX_KEY_VALUE_H

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  VSX_KEY_VALUE_EVENT_HEADER,
  VSX_KEY_VALUE_EVENT_PROPERTY
} VsxKeyValueEvent;

typedef void (* VsxKeyValueCallback) (VsxKeyValueEvent event,
                                      int line_number,
                                      const char *key,
                                      const char *value,
                                      void *user_data);

typedef void (* VsxKeyValueErrorCallback) (const char *message,
                                           void *user_data);

void
vsx_key_value_load (FILE * file,
                    VsxKeyValueCallback func,
                    VsxKeyValueErrorCallback error_func,
                    void *user_data);

bool
vsx_key_value_parse_bool_value (int line_number,
                                const char *value,
                                bool *result);

bool
vsx_key_value_parse_int_value (int line_number,
                               const char *value_string,
                               int64_t max,
                               int64_t *result);

#endif /* VSX_KEY_VALUE_H */
