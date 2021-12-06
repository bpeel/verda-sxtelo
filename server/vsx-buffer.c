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

#include "config.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "vsx-buffer.h"

void
vsx_buffer_init(struct vsx_buffer *buffer)
{
        static const struct vsx_buffer init = VSX_BUFFER_STATIC_INIT;

        *buffer = init;
}

void
vsx_buffer_ensure_size(struct vsx_buffer *buffer,
                       size_t size)
{
        size_t new_size = MAX(buffer->size, 1);

        while (new_size < size)
                new_size *= 2;

        if (new_size != buffer->size) {
                buffer->data = vsx_realloc(buffer->data, new_size);
                buffer->size = new_size;
        }
}

void
vsx_buffer_set_length(struct vsx_buffer *buffer,
                      size_t length)
{
        vsx_buffer_ensure_size(buffer, length);
        buffer->length = length;
}

void
vsx_buffer_append_vprintf(struct vsx_buffer *buffer,
                          const char *format,
                          va_list ap)
{
        va_list apcopy;
        int length;

        vsx_buffer_ensure_size(buffer, buffer->length + 16);

        va_copy(apcopy, ap);
        length = vsnprintf((char *) buffer->data + buffer->length,
                           buffer->size - buffer->length,
                           format,
                           ap);

        if (length >= buffer->size - buffer->length) {
                vsx_buffer_ensure_size(buffer, buffer->length + length + 1);
                vsnprintf((char *) buffer->data + buffer->length,
                          buffer->size - buffer->length,
                          format,
                          apcopy);
        }

        va_end(apcopy);

        buffer->length += length;
}

VSX_PRINTF_FORMAT(2, 3) void
vsx_buffer_append_printf(struct vsx_buffer *buffer,
                         const char *format,
                         ...)
{
        va_list ap;

        va_start(ap, format);
        vsx_buffer_append_vprintf(buffer, format, ap);
        va_end(ap);
}

void
vsx_buffer_append(struct vsx_buffer *buffer,
                  const void *data,
                  size_t length)
{
        vsx_buffer_ensure_size(buffer, buffer->length + length);
        memcpy(buffer->data + buffer->length, data, length);
        buffer->length += length;
}

void
vsx_buffer_append_string(struct vsx_buffer *buffer,
                         const char *str)
{
        vsx_buffer_append(buffer, str, strlen(str) + 1);
        buffer->length--;
}

void
vsx_buffer_destroy(struct vsx_buffer *buffer)
{
        vsx_free(buffer->data);
}
