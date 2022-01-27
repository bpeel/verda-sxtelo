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

#ifndef VSX_FONT_H
#define VSX_FONT_H

#include <stdint.h>

#include "vsx-error.h"
#include "vsx-gl.h"
#include "vsx-asset.h"
#include "vsx-glyph-hash.h"

struct vsx_font;
struct vsx_font_library;

extern struct vsx_error_domain
vsx_font_error;

enum vsx_font_error {
        VSX_FONT_ERROR_INVALID,
        VSX_FONT_ERROR_LIBRARY,
};

enum vsx_font_type {
        VSX_FONT_TYPE_LABEL,

        VSX_FONT_N_TYPES,
};

struct vsx_font_metrics {
        float ascender, descender, height;
};

struct vsx_font_library *
vsx_font_library_new(struct vsx_gl *gl,
                     struct vsx_asset_manager *asset_manager,
                     int dpi,
                     struct vsx_error **error);

struct vsx_font *
vsx_font_library_get_font(struct vsx_font_library *library,
                          enum vsx_font_type type);

unsigned
vsx_font_look_up_glyph(struct vsx_font *font,
                       uint32_t unicode);

struct vsx_glyph_hash_entry *
vsx_font_prepare_glyph(struct vsx_font *font,
                       unsigned glyph_index);

void
vsx_font_get_metrics(struct vsx_font *font,
                     struct vsx_font_metrics *metrics);

void
vsx_font_library_free(struct vsx_font_library *library);

#endif /* VSX_FONT_H */
