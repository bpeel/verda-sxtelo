/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2021, 2022  Neil Roberts
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

#ifndef VSX_TOOLBOX_H
#define VSX_TOOLBOX_H

#include "vsx-gl.h"
#include "vsx-main-thread.h"
#include "vsx-map-buffer.h"
#include "vsx-quad-tool.h"
#include "vsx-shader-data.h"
#include "vsx-image-loader.h"
#include "vsx-font.h"
#include "vsx-shadow-painter.h"
#include "vsx-tile-tool.h"
#include "vsx-paint-state.h"
#include "vsx-shell-interface.h"

struct vsx_toolbox {
        struct vsx_gl *gl;
        struct vsx_main_thread *main_thread;
        struct vsx_map_buffer *map_buffer;
        struct vsx_quad_tool *quad_tool;
        struct vsx_shader_data shader_data;
        struct vsx_image_loader *image_loader;
        struct vsx_font_library *font_library;
        struct vsx_shadow_painter *shadow_painter;
        struct vsx_tile_tool *tile_tool;
        struct vsx_paint_state paint_state;
        struct vsx_shell_interface *shell;
};

#endif /* VSX_TOOLBOX_H */
