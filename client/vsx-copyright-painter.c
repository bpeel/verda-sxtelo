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

#include "config.h"

#include "vsx-copyright-painter.h"

#include "vsx-layout.h"
#include "vsx-util.h"

struct vsx_copyright_painter {
        struct vsx_game_state *game_state;

        struct vsx_toolbox *toolbox;

        struct vsx_array_object *vao;
        GLuint vbo;

        bool position_dirty;

        int border;
        int dialog_x, dialog_y;
        int dialog_width, dialog_height;
        float translation[2];

        struct vsx_layout_paint_position layout;

        struct vsx_shadow_painter_shadow *shadow;
        struct vsx_listener shadow_painter_ready_listener;
};

struct vertex {
        int16_t x, y;
};

static const char
copyright_text[] =
        "Copyright © 2022 Neil Roberts. All rights reserved.\n"
        "\n"
        "Portions of this software are copyright © 2022 The FreeType "
        "Project (www.freetype.org).  All rights reserved.\n"
        "\n"
        "The Luna Sans font is copyright 2013 The Alegreya Sans Project "
        "Authors.";

#define N_QUADS 1
#define N_VERTICES (N_QUADS * 4)

/* Max width of the text in mm */
#define PARAGRAPH_WIDTH 60
/* Border size around the paragraphs in mm */
#define BORDER 3

static void
shadow_painter_ready_cb(struct vsx_listener *listener,
                        void *user_data)
{
        struct vsx_copyright_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_copyright_painter,
                                 shadow_painter_ready_listener);

        painter->toolbox->shell->queue_redraw_cb(painter->toolbox->shell);
}

static void
create_buffer(struct vsx_copyright_painter *painter)
{
        int w = painter->dialog_width;
        int h = painter->dialog_height;

        struct vertex vertices[N_VERTICES] = {
                { 0, 0 },
                { 0, h },
                { w, 0 },
                { w, h },
        };

        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glGenBuffers(1, &painter->vbo);
        gl->glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);
        gl->glBufferData(GL_ARRAY_BUFFER,
                         sizeof vertices,
                         vertices,
                         GL_STATIC_DRAW);

        painter->vao = vsx_array_object_new(gl);

        vsx_array_object_set_attribute(painter->vao,
                                       gl,
                                       VSX_SHADER_DATA_ATTRIB_POSITION,
                                       2, /* size */
                                       GL_SHORT,
                                       false, /* normalized */
                                       sizeof (struct vertex),
                                       painter->vbo,
                                       offsetof(struct vertex, x));
}

static void
create_layout(struct vsx_copyright_painter *painter)
{
        int width = (PARAGRAPH_WIDTH *
                     painter->toolbox->paint_state.dpi *
                     10 / 254);

        painter->layout.layout = vsx_layout_new(painter->toolbox);

        char *text = vsx_strconcat(copyright_text,
#ifdef APP_VERSION
                                   "\n"
                                   "\n"
                                   "Version " APP_VERSION,
#endif
                                   NULL);

        vsx_layout_set_text(painter->layout.layout, text);

        vsx_free(text);

        vsx_layout_set_font(painter->layout.layout, VSX_FONT_TYPE_LABEL);
        vsx_layout_set_width(painter->layout.layout, width);
        vsx_layout_prepare(painter->layout.layout);
}

static void
get_size(struct vsx_copyright_painter *painter)
{
        painter->border = BORDER * painter->toolbox->paint_state.dpi * 10 / 254;

        const struct vsx_layout_extents *extents =
                vsx_layout_get_logical_extents(painter->layout.layout);

        painter->dialog_width = extents->right + painter->border * 2;
        painter->dialog_height = (extents->top +
                                  extents->bottom +
                                  painter->border * 2);
}

static void *
create_cb(struct vsx_game_state *game_state,
          struct vsx_toolbox *toolbox)
{
        struct vsx_copyright_painter *painter = vsx_calloc(sizeof *painter);

        painter->position_dirty = true;

        painter->game_state = game_state;
        painter->toolbox = toolbox;

        painter->shadow_painter_ready_listener.notify =
                shadow_painter_ready_cb;
        struct vsx_shadow_painter *shadow_painter = toolbox->shadow_painter;
        vsx_signal_add(vsx_shadow_painter_get_ready_signal(shadow_painter),
                       &painter->shadow_painter_ready_listener);

        create_layout(painter);

        get_size(painter);

        create_buffer(painter);

        painter->shadow =
                vsx_shadow_painter_create_shadow(shadow_painter,
                                                 painter->dialog_width,
                                                 painter->dialog_height);

        return painter;
}

static void
fb_size_changed_cb(void *painter_data)
{
        struct vsx_copyright_painter *painter = painter_data;

        painter->position_dirty = true;
}

static void
paint_background(struct vsx_copyright_painter *painter)
{
        const struct vsx_shader_data *shader_data =
                &painter->toolbox->shader_data;

        const struct vsx_shader_data_program_data *program =
                shader_data->programs + VSX_SHADER_DATA_PROGRAM_SOLID;

        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glUseProgram(program->program);

        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        gl->glUniformMatrix2fv(program->matrix_uniform,
                               1, /* count */
                               GL_FALSE, /* transpose */
                               paint_state->pixel_matrix);
        gl->glUniform2f(program->translation_uniform,
                        painter->translation[0],
                        painter->translation[1]);

        vsx_array_object_bind(painter->vao, gl);

        gl->glUniform3f(program->color_uniform, 1.0f, 1.0f, 1.0f);

        gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, N_VERTICES);
}

static void
ensure_position(struct vsx_copyright_painter *painter)
{
        if (!painter->position_dirty)
                return;

        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        vsx_paint_state_ensure_layout(paint_state);

        painter->dialog_x = (paint_state->pixel_width / 2 -
                             painter->dialog_width / 2);
        painter->dialog_y = (paint_state->pixel_height / 2 -
                             painter->dialog_height / 2);

        vsx_paint_state_offset_pixel_translation(paint_state,
                                                 painter->dialog_x,
                                                 painter->dialog_y,
                                                 painter->translation);

        const struct vsx_layout_extents *extents =
                vsx_layout_get_logical_extents(painter->layout.layout);

        painter->layout.x = painter->dialog_x + painter->border;
        painter->layout.y = painter->dialog_y + extents->top + painter->border;

        painter->position_dirty = false;
}

static void
paint_cb(void *painter_data)
{
        struct vsx_copyright_painter *painter = painter_data;

        if (!vsx_shadow_painter_is_ready(painter->toolbox->shadow_painter))
                return;

        ensure_position(painter);

        vsx_shadow_painter_paint(painter->toolbox->shadow_painter,
                                 painter->shadow,
                                 &painter->toolbox->shader_data,
                                 painter->toolbox->paint_state.pixel_matrix,
                                 painter->translation);

        paint_background(painter);

        vsx_layout_paint_multiple(&painter->layout, 1);
}

static bool
handle_click(struct vsx_copyright_painter *painter,
             const struct vsx_input_event *event)
{
        vsx_game_state_close_dialog(painter->game_state);

        return true;
}

static bool
input_event_cb(void *painter_data,
               const struct vsx_input_event *event)
{
        struct vsx_copyright_painter *painter = painter_data;

        switch (event->type) {
        case VSX_INPUT_EVENT_TYPE_DRAG_START:
        case VSX_INPUT_EVENT_TYPE_DRAG:
        case VSX_INPUT_EVENT_TYPE_ZOOM_START:
        case VSX_INPUT_EVENT_TYPE_ZOOM:
                return false;

        case VSX_INPUT_EVENT_TYPE_CLICK:
                return handle_click(painter, event);
        }

        return false;
}

static void
free_cb(void *painter_data)
{
        struct vsx_copyright_painter *painter = painter_data;

        vsx_list_remove(&painter->shadow_painter_ready_listener.link);

        struct vsx_gl *gl = painter->toolbox->gl;

        if (painter->vao)
                vsx_array_object_free(painter->vao, gl);
        if (painter->vbo)
                gl->glDeleteBuffers(1, &painter->vbo);

        vsx_layout_free(painter->layout.layout);

        vsx_shadow_painter_free_shadow(painter->toolbox->shadow_painter,
                                       painter->shadow);
        vsx_free(painter);
}

const struct vsx_painter
vsx_copyright_painter = {
        .create_cb = create_cb,
        .fb_size_changed_cb = fb_size_changed_cb,
        .paint_cb = paint_cb,
        .input_event_cb = input_event_cb,
        .free_cb = free_cb,
};
