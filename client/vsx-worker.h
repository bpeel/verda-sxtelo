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

#ifndef VSX_WORKER_H
#define VSX_WORKER_H

#include "vsx-connection.h"
#include "vsx-error.h"

struct vsx_worker;

struct vsx_worker *
vsx_worker_new(struct vsx_connection *connection,
               struct vsx_error **error);

void
vsx_worker_queue_address_resolve(struct vsx_worker *worker,
                                 const char *address,
                                 int port);

void
vsx_worker_lock(struct vsx_worker *worker);

void
vsx_worker_unlock(struct vsx_worker *worker);

void
vsx_worker_free(struct vsx_worker *worker);

#endif /* VSX_WORKER_H */
