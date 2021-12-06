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

#include "config.h"

#include "vsx-file-error.h"

#include <errno.h>

struct vsx_error_domain
vsx_file_error;

enum vsx_file_error
vsx_file_error_from_errno(int errnum)
{
        switch (errnum) {
        case EEXIST:
                return VSX_FILE_ERROR_EXIST;
        case EISDIR:
                return VSX_FILE_ERROR_ISDIR;
        case EACCES:
                return VSX_FILE_ERROR_ACCES;
        case ENAMETOOLONG:
                return VSX_FILE_ERROR_NAMETOOLONG;
        case ENOENT:
                return VSX_FILE_ERROR_NOENT;
        case ENOTDIR:
                return VSX_FILE_ERROR_NOTDIR;
        case EAGAIN:
#if EAGAIN != EWOULDBLOCK
        case EWOULDBLOCK:
#endif
                return VSX_FILE_ERROR_AGAIN;
        case EINTR:
                return VSX_FILE_ERROR_INTR;
        case EPERM:
                return VSX_FILE_ERROR_PERM;
        case EPFNOSUPPORT:
                return VSX_FILE_ERROR_PFNOSUPPORT;
        case EAFNOSUPPORT:
                return VSX_FILE_ERROR_AFNOSUPPORT;
        case EMFILE:
                return VSX_FILE_ERROR_MFILE;
        case EBADF:
                return VSX_FILE_ERROR_BADF;
        }

        return VSX_FILE_ERROR_OTHER;
}

VSX_PRINTF_FORMAT(3, 4) void
vsx_file_error_set(struct vsx_error **error,
                   int errnum,
                   const char *format,
                   ...)
{
        va_list ap;

        va_start(ap, format);
        vsx_set_error_va_list(error,
                              &vsx_file_error,
                              vsx_file_error_from_errno(errnum),
                              format,
                              ap);
        va_end(ap);
}
