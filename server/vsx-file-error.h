/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2013, 2020  Neil Roberts
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

#ifndef VSX_FILE_ERROR_H
#define VSX_FILE_ERROR_H

#include "vsx-error.h"

extern struct vsx_error_domain
vsx_file_error;

enum vsx_file_error {
        VSX_FILE_ERROR_EXIST,
        VSX_FILE_ERROR_ISDIR,
        VSX_FILE_ERROR_ACCES,
        VSX_FILE_ERROR_NAMETOOLONG,
        VSX_FILE_ERROR_NOENT,
        VSX_FILE_ERROR_NOTDIR,
        VSX_FILE_ERROR_AGAIN,
        VSX_FILE_ERROR_INTR,
        VSX_FILE_ERROR_PERM,
        VSX_FILE_ERROR_PFNOSUPPORT,
        VSX_FILE_ERROR_AFNOSUPPORT,
        VSX_FILE_ERROR_MFILE,
        VSX_FILE_ERROR_BADF,

        VSX_FILE_ERROR_OTHER
};

enum vsx_file_error
vsx_file_error_from_errno(int errnum);

VSX_PRINTF_FORMAT(3, 4) void
vsx_file_error_set(struct vsx_error **error,
                   int errnum,
                   const char *format,
                   ...);

#endif /* VSX_FILE_ERROR_H */
