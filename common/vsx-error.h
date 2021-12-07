/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2013, 2019  Neil Roberts
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

#ifndef VSX_ERROR_H
#define VSX_ERROR_H

#include <stdarg.h>

#include "vsx-util.h"

/* Exception handling mechanism inspired by glib's GError */

struct vsx_error_domain {
        int stub;
};

struct vsx_error {
        struct vsx_error_domain *domain;
        int code;
        char message[1];
};

void
vsx_set_error_va_list(struct vsx_error **error_out,
                      struct vsx_error_domain *domain,
                      int code,
                      const char *format,
                      va_list ap);

VSX_PRINTF_FORMAT(4, 5) void
vsx_set_error(struct vsx_error **error,
              struct vsx_error_domain *domain,
              int code,
              const char *format,
              ...);

void
vsx_error_free(struct vsx_error *error);

void
vsx_error_clear(struct vsx_error **error);

void
vsx_error_propagate(struct vsx_error **error,
                    struct vsx_error *other);

#endif /* VSX_ERROR_H */
