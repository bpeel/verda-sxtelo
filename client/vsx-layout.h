/*
 * Vsxeto - A geolocalisation game.
 * Copyright (C) 2021  Neil Roberts
 */

#ifndef VSX_LAYOUT_H
#define VSX_LAYOUT_H

#include "vsx-font.h"
#include "vsx-toolbox.h"

struct vsx_layout;

struct vsx_layout_extents {
        /* Extents around the origin when painted */
        float left, right;
        float top, bottom;

        int n_lines;
};

struct vsx_layout_paint_position {
        struct vsx_layout *layout;
        int x, y;
};

struct vsx_layout_paint_params {
        const struct vsx_layout_paint_position *layouts;
        size_t n_layouts;
        const float *matrix;
        float translation_x, translation_y;
        float r, g, b;
};

struct vsx_layout *
vsx_layout_new(struct vsx_toolbox *toolbox);

void
vsx_layout_set_text(struct vsx_layout *layout,
                    const char *text);

void
vsx_layout_set_font(struct vsx_layout *layout,
                    enum vsx_font_type font);

void
vsx_layout_set_width(struct vsx_layout *layout,
                     unsigned width);

void
vsx_layout_prepare(struct vsx_layout *layout);

const struct vsx_layout_extents *
vsx_layout_get_logical_extents(struct vsx_layout *layout);

void
vsx_layout_paint_params(const struct vsx_layout_paint_params *params);

void
vsx_layout_paint_multiple(const struct vsx_layout_paint_position *layouts,
                          size_t n_layouts,
                          float r, float g, float b);

void
vsx_layout_paint(struct vsx_layout *layout,
                 int x, int y,
                 float r, float g, float b);

void
vsx_layout_free(struct vsx_layout *layout);

#endif /* VSX_LAYOUT_H */
