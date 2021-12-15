/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2014, 2021  Neil Roberts
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

#ifndef VSX_SHADER_DATA_H
#define VSX_SHADER_DATA_H

#include <stdbool.h>

#include "vsx-asset.h"
#include "vsx-error.h"
#include "vsx-gl.h"

extern struct vsx_error_domain
vsx_shader_data_error;

enum vsx_shader_data_program {
        VSX_SHADER_DATA_PROGRAM_SOLID,
        VSX_SHADER_DATA_N_PROGRAMS
};

enum vsx_shader_data_attrib {
        VSX_SHADER_DATA_ATTRIB_POSITION,
        VSX_SHADER_DATA_ATTRIB_TEX_COORD,
        VSX_SHADER_DATA_ATTRIB_COLOR,
        VSX_SHADER_DATA_ATTRIB_NORMAL,
};

enum vsx_shader_data_error {
        VSX_SHADER_DATA_ERROR_COMPILATION_FAILED,
        VSX_SHADER_DATA_ERROR_LINK_FAILED,
        VSX_SHADER_DATA_ERROR_FILE,
};

struct vsx_shader_data {
        GLuint programs[VSX_SHADER_DATA_N_PROGRAMS];
};

bool
vsx_shader_data_init(struct vsx_shader_data *data,
                     struct vsx_asset_manager *asset_manager,
                     struct vsx_error **error);

void
vsx_shader_data_destroy(struct vsx_shader_data *data);

#endif /* VSX_SHADER_DATA_H */
