/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2019, 2021  Neil Roberts
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

#ifndef __VSX_CONFIG_H__
#define __VSX_CONFIG_H__

#include <glib.h>

#include "vsx-list.h"

typedef enum
{
  VSX_CONFIG_ERROR_IO
} VsxConfigError;

typedef struct
{
  VsxList link;
  char *address;
  int port;
  char *certificate;
  char *private_key;
  char *private_key_password;
} VsxConfigServer;

typedef struct
{
  char *log_file;
  char *user;
  char *group;
  VsxList servers;
} VsxConfig;

#define VSX_CONFIG_ERROR (vsx_config_error_quark ())

VsxConfig *vsx_config_load (const char *filename, GError **error);

void vsx_config_free (VsxConfig *config);

GQuark vsx_config_error_quark (void);

#endif /* __VSX_CONFIG_H__ */
