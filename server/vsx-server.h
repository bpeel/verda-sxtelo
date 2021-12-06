/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2011  Neil Roberts
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

#ifndef __VSX_SERVER_H__
#define __VSX_SERVER_H__

#include <stdbool.h>

#include "vsx-config.h"
#include "vsx-error.h"

typedef struct _VsxServer VsxServer;

typedef enum
{
  VSX_SERVER_ERROR_INVALID_ADDRESS,
} VsxServerError;

extern struct vsx_error_domain
vsx_server_error;

VsxServer *
vsx_server_new (void);

bool
vsx_server_add_config (VsxServer *server,
                       VsxConfigServer *server_config,
                       int fd_override,
                       struct vsx_error **error);

bool
vsx_server_run (VsxServer *server,
                struct vsx_error **error);

void
vsx_server_free (VsxServer *mc);

#endif /* __VSX_SERVER_H__ */
