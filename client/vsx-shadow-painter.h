/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2022  Neil Roberts
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

#ifndef VSX_SHADOW_PAINTER_H
#define VSX_SHADOW_PAINTER_H

#include <stdbool.h>

#include "vsx-gl.h"
#include "vsx-image-loader.h"
#include "vsx-map-buffer.h"
#include "vsx-shader-data.h"
#include "vsx-signal.h"
#include "vsx-shell-interface.h"

struct vsx_shadow_painter;
struct vsx_shadow_painter_shadow;

struct vsx_shadow_painter *
vsx_shadow_painter_new(struct vsx_gl *gl,
                       struct vsx_shell_interface *shell,
                       struct vsx_image_loader *image_loader,
                       struct vsx_map_buffer *map_buffer,
                       int dpi);

bool
vsx_shadow_painter_is_ready(struct vsx_shadow_painter *painter);

struct vsx_signal *
vsx_shadow_painter_get_ready_signal(struct vsx_shadow_painter *painter);

void
vsx_shadow_painter_paint(struct vsx_shadow_painter *painter,
                         struct vsx_shadow_painter_shadow *shadow,
                         const struct vsx_shader_data *shader_data,
                         const GLfloat *matrix,
                         const GLfloat *translation);

struct vsx_shadow_painter_shadow *
vsx_shadow_painter_create_shadow(struct vsx_shadow_painter *painter,
                                 int w, int h);

void
vsx_shadow_painter_free_shadow(struct vsx_shadow_painter *painter,
                               struct vsx_shadow_painter_shadow *shadow);

void
vsx_shadow_painter_free(struct vsx_shadow_painter *painter);

#endif /* VSX_SHADOW_PAINTER_H */
