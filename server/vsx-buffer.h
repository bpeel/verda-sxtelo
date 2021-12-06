/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2013  Neil Roberts
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

#ifndef VSX_BUFFER_H
#define VSX_BUFFER_H

#include <stdint.h>
#include <stdarg.h>

#include "vsx-util.h"

struct vsx_buffer {
        uint8_t *data;
        size_t length;
        size_t size;
};

#define VSX_BUFFER_STATIC_INIT { .data = NULL, .length = 0, .size = 0 }

void
vsx_buffer_init(struct vsx_buffer *buffer);

void
vsx_buffer_ensure_size(struct vsx_buffer *buffer,
                       size_t size);

void
vsx_buffer_set_length(struct vsx_buffer *buffer,
                      size_t length);

VSX_PRINTF_FORMAT(2, 3) void
vsx_buffer_append_printf(struct vsx_buffer *buffer,
                         const char *format,
                         ...);

void
vsx_buffer_append_vprintf(struct vsx_buffer *buffer,
                          const char *format,
                          va_list ap);

void
vsx_buffer_append(struct vsx_buffer *buffer,
                  const void *data,
                  size_t length);

static inline void
vsx_buffer_append_c(struct vsx_buffer *buffer,
                    char c)
{
        if (buffer->size > buffer->length)
                buffer->data[buffer->length++] = c;
        else
                vsx_buffer_append(buffer, &c, 1);
}

void
vsx_buffer_append_string(struct vsx_buffer *buffer,
                         const char *str);

void
vsx_buffer_destroy(struct vsx_buffer *buffer);

#endif /* VSX_BUFFER_H */
