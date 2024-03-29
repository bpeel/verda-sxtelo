/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2021  Neil Roberts
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

#ifndef VSX_SSL_ERROR_H
#define VSX_SSL_ERROR_H

#include "vsx-error.h"

extern struct vsx_error_domain
vsx_ssl_error;

typedef enum
{
    VSX_SSL_ERROR_OTHER
} VsxSslError;

VsxSslError
vsx_ssl_error_from_errno (unsigned long errnum);

void
vsx_ssl_error_set (struct vsx_error **error);

#endif /* VSX_SSL_ERROR_H */
