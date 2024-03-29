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

#include "vsx-socket.h"

#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include "vsx-file-error.h"

bool
vsx_socket_set_nonblock(int sock,
                        struct vsx_error **error)
{
        int flags;

        flags = fcntl(sock, F_GETFL, 0);

        if (flags == -1 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
                vsx_file_error_set(error,
                                   errno,
                                   "Error setting non-blocking mode: %s",
                                   strerror(errno));
                return false;
        }

        return true;
}
