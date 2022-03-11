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

#include "vsx-name-painter.h"

#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "vsx-map-buffer.h"
#include "vsx-gl.h"
#include "vsx-array-object.h"
#include "vsx-layout.h"
#include "vsx-buffer.h"

struct vsx_name_painter_rect {
        int x, y;
        int w, h;
};

struct vsx_name_painter {
        struct vsx_game_state *game_state;
        struct vsx_listener modified_listener;

        struct vsx_toolbox *toolbox;

        struct vsx_array_object *vao;
        GLuint vbo;
        struct vsx_quad_tool_buffer *quad_buffer;

        bool layout_dirty;

        struct vsx_layout_paint_position layouts[4];

        int dialog_gap;

        struct vsx_name_painter_rect dialog_rect;
        struct vsx_name_painter_rect links_rect;

        int button_border;

        struct vsx_name_painter_rect button_rect;

        struct vsx_listener name_size_listener;

        /* This is using its own pixel transformation because we don’t
         * want to take into account the board rotation.
         */
        GLfloat matrix[4];

        struct vsx_shadow_painter_shadow *dialog_shadow, *links_shadow;
        struct vsx_listener shadow_painter_ready_listener;
};

struct vertex {
        float x, y;
};

enum layout {
        LAYOUT_NOTE,
        LAYOUT_BUTTON,
        LAYOUT_PRIVACY_POLICY,
        LAYOUT_COPYRIGHT,
};

static const enum layout
link_layouts[] = {
        LAYOUT_PRIVACY_POLICY,
        LAYOUT_COPYRIGHT,
};

#define N_QUADS 3
#define N_VERTICES (N_QUADS * 4)

/* Gap in MM around the dialog */
#define DIALOG_GAP 5
/* Border in MM inside the dialog around the contents */
#define INNER_BORDER 5

/* Border around the button label in MM */
#define BUTTON_BORDER 2

#define FONT VSX_FONT_TYPE_LABEL

#define PRIVACY_POLICY_LINK_FORMAT \
        "https://gemelo.org/grabagram/privacy-policy.%s.html"

static void
name_size_cb(struct vsx_listener *listener,
             void *user_data)
{
        struct vsx_name_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_name_painter,
                                 name_size_listener);

        painter->layout_dirty = true;
        painter->toolbox->shell->queue_redraw_cb(painter->toolbox->shell);
}

static void
modified_cb(struct vsx_listener *listener,
            void *user_data)
{
        struct vsx_name_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_name_painter,
                                 modified_listener);
        const struct vsx_game_state_modified_event *event = user_data;
        struct vsx_shell_interface *shell = painter->toolbox->shell;

        switch (event->type) {
        case VSX_GAME_STATE_MODIFIED_TYPE_START_TYPE:
        case VSX_GAME_STATE_MODIFIED_TYPE_LANGUAGE:
                painter->layout_dirty = true;
                shell->queue_redraw_cb(shell);
                break;
        default:
                break;
        }
}

static void
shadow_painter_ready_cb(struct vsx_listener *listener,
                        void *user_data)
{
        struct vsx_name_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_name_painter,
                                 shadow_painter_ready_listener);

        painter->toolbox->shell->queue_redraw_cb(painter->toolbox->shell);
}

static void
clear_dialog_shadow(struct vsx_name_painter *painter)
{
        if (painter->dialog_shadow == NULL)
                return;

        vsx_shadow_painter_free_shadow(painter->toolbox->shadow_painter,
                                       painter->dialog_shadow);
        painter->dialog_shadow = NULL;
}

static void
create_dialog_shadow(struct vsx_name_painter *painter)
{
        clear_dialog_shadow(painter);

        struct vsx_shadow_painter *shadow_painter =
                painter->toolbox->shadow_painter;

        int w = painter->dialog_rect.w;
        int h = painter->dialog_rect.h;

        painter->dialog_shadow =
                vsx_shadow_painter_create_shadow(shadow_painter, w, h);
}

static void
clear_links_shadow(struct vsx_name_painter *painter)
{
        if (painter->links_shadow == NULL)
                return;

        vsx_shadow_painter_free_shadow(painter->toolbox->shadow_painter,
                                       painter->links_shadow);
        painter->links_shadow = NULL;
}

static void
update_link_layouts(struct vsx_name_painter *painter)
{
        /* Add a line for each blank */
        int n_lines = VSX_N_ELEMENTS(link_layouts) - 1;

        int rightmost = 0;

        for (int i = 0; i < VSX_N_ELEMENTS(link_layouts); i++) {
                struct vsx_layout_paint_position *pos =
                        painter->layouts + link_layouts[i];
                const struct vsx_layout_extents *extents =
                        vsx_layout_get_logical_extents(pos->layout);

                if (rightmost < extents->right)
                        rightmost = extents->right;

                n_lines += extents->n_lines;
        }

        struct vsx_font_library *font_library = painter->toolbox->font_library;
        struct vsx_font *font = vsx_font_library_get_font(font_library, FONT);

        struct vsx_font_metrics font_metrics;
        vsx_font_get_metrics(font, &font_metrics);

        const struct vsx_paint_state *paint_state =
                &painter->toolbox->paint_state;

        painter->links_rect.w = rightmost + painter->button_border * 2;
        painter->links_rect.h = (n_lines * font_metrics.height +
                                 painter->button_border * 2);
        painter->links_rect.x = (paint_state->width -
                                 painter->links_rect.w -
                                 painter->dialog_gap);
        painter->links_rect.y = (paint_state->height -
                                 painter->links_rect.h -
                                 painter->dialog_gap);

        int y = painter->links_rect.y + painter->button_border;

        for (int i = 0; i < VSX_N_ELEMENTS(link_layouts); i++) {
                struct vsx_layout_paint_position *pos =
                        painter->layouts + link_layouts[i];
                const struct vsx_layout_extents *extents =
                        vsx_layout_get_logical_extents(pos->layout);

                pos->x = painter->links_rect.x + painter->button_border;
                pos->y = y + extents->top;

                y += (extents->n_lines + 1) * font_metrics.height;
        }

        clear_links_shadow(painter);

        struct vsx_shadow_painter *shadow_painter =
                painter->toolbox->shadow_painter;
        painter->links_shadow =
                vsx_shadow_painter_create_shadow(shadow_painter,
                                                 painter->links_rect.w,
                                                 painter->links_rect.h);
}

static void
store_quad(struct vertex *v, const struct vsx_name_painter_rect *rect)
{
        v->x = rect->x;
        v->y = rect->y;
        v++;

        v->x = rect->x;
        v->y = rect->y + rect->h;
        v++;

        v->x = rect->x + rect->w;
        v->y = rect->y;
        v++;

        v->x = rect->x + rect->w;
        v->y = rect->y + rect->h;
        v++;
}

static void
update_vertices(struct vsx_name_painter *painter)
{
        struct vertex vertices[N_VERTICES];

        store_quad(vertices, &painter->dialog_rect);
        store_quad(vertices + 4, &painter->links_rect);
        store_quad(vertices + 8, &painter->button_rect);

        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);
        gl->glBufferData(GL_ARRAY_BUFFER,
                         sizeof vertices,
                         vertices,
                         GL_DYNAMIC_DRAW);
}

static void
create_buffer(struct vsx_name_painter *painter)
{
        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glGenBuffers(1, &painter->vbo);
        gl->glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);
        gl->glBufferData(GL_ARRAY_BUFFER,
                         N_VERTICES * sizeof (struct vertex),
                         NULL, /* data */
                         GL_DYNAMIC_DRAW);

        painter->vao = vsx_array_object_new(gl);

        vsx_array_object_set_attribute(painter->vao,
                                       gl,
                                       VSX_SHADER_DATA_ATTRIB_POSITION,
                                       2, /* size */
                                       GL_FLOAT,
                                       false, /* normalized */
                                       sizeof (struct vertex),
                                       painter->vbo,
                                       offsetof(struct vertex, x));

        painter->quad_buffer =
                vsx_quad_tool_get_buffer(painter->toolbox->quad_tool,
                                         painter->vao,
                                         N_QUADS);
}

static void *
create_cb(struct vsx_game_state *game_state,
          struct vsx_toolbox *toolbox)
{
        struct vsx_name_painter *painter = vsx_calloc(sizeof *painter);

        /* Convert the measurements from mm to pixels */
        int dpi = toolbox->paint_state.dpi;
        painter->dialog_gap = DIALOG_GAP * dpi * 10 / 254;
        painter->button_border = BUTTON_BORDER * dpi * 10 / 254;

        painter->game_state = game_state;
        painter->toolbox = toolbox;
        painter->layout_dirty = true;

        create_buffer(painter);
        update_vertices(painter);

        for (int i = 0; i < VSX_N_ELEMENTS(painter->layouts); i++) {
                painter->layouts[i].layout = vsx_layout_new(toolbox);
                vsx_layout_set_font(painter->layouts[i].layout, FONT);
        }

        painter->layouts[LAYOUT_BUTTON].r = 1.0f;
        painter->layouts[LAYOUT_BUTTON].g = 1.0f;
        painter->layouts[LAYOUT_BUTTON].b = 1.0f;

        for (int i = 0; i < VSX_N_ELEMENTS(link_layouts); i++) {
                struct vsx_layout_paint_position *pos =
                        painter->layouts + link_layouts[i];
                pos->r = 0.106f;
                pos->g = 0.561f;
                pos->b = 0.871f;
        }

        vsx_layout_set_text(painter->layouts[LAYOUT_COPYRIGHT].layout,
                            "Copyright © 2022 Neil Roberts");

        painter->modified_listener.notify = modified_cb;
        vsx_signal_add(vsx_game_state_get_modified_signal(game_state),
                       &painter->modified_listener);

        painter->shadow_painter_ready_listener.notify =
                shadow_painter_ready_cb;
        struct vsx_shadow_painter *shadow_painter = toolbox->shadow_painter;
        vsx_signal_add(vsx_shadow_painter_get_ready_signal(shadow_painter),
                       &painter->shadow_painter_ready_listener);

        painter->name_size_listener.notify = name_size_cb;
        vsx_signal_add(&toolbox->shell->name_size_signal,
                       &painter->name_size_listener);

        return painter;
}

static void
fb_size_changed_cb(void *painter_data)
{
        struct vsx_name_painter *painter = painter_data;

        painter->layout_dirty = true;
}

static void
update_transform(struct vsx_name_painter *painter,
                 const struct vsx_paint_state *paint_state)
{
        GLfloat *matrix = painter->matrix;

        /* This deliberately doesn’t take into account the board
         * rotation because we want the name dialog to have the same
         * orientation as the onscreen keyboard.
         */
        matrix[0] = 2.0f / paint_state->width;
        matrix[1] = 0.0f;
        matrix[2] = 0.0f;
        matrix[3] = -2.0f / paint_state->height;
}

static void
update_layout_text(struct vsx_name_painter *painter)
{
        enum vsx_text_language language =
                vsx_game_state_get_language(painter->game_state);
        enum vsx_text note_text, button_text;

        vsx_layout_set_text(painter->layouts[LAYOUT_PRIVACY_POLICY].layout,
                            vsx_text_get(language, VSX_TEXT_PRIVACY_POLICY));

        switch (vsx_game_state_get_start_type(painter->game_state)) {
        case VSX_GAME_STATE_START_TYPE_NEW_GAME:
                note_text = VSX_TEXT_ENTER_NAME_NEW_GAME;
                button_text = VSX_TEXT_NAME_BUTTON_NEW_GAME;
                goto found;
        case VSX_GAME_STATE_START_TYPE_JOIN_GAME:
                note_text = VSX_TEXT_ENTER_NAME_JOIN_GAME;
                button_text = VSX_TEXT_NAME_BUTTON_JOIN_GAME;
                goto found;
        }

        assert(!"Unknown name type");

        return;

found:
        vsx_layout_set_text(painter->layouts[LAYOUT_NOTE].layout,
                            vsx_text_get(language, note_text));
        vsx_layout_set_text(painter->layouts[LAYOUT_BUTTON].layout,
                            vsx_text_get(language, button_text));
}

static void
prepare_cb(void *painter_data)
{
        struct vsx_name_painter *painter = painter_data;

        if (!painter->layout_dirty)
                return;

        struct vsx_paint_state *paint_state =
                &painter->toolbox->paint_state;
        vsx_paint_state_ensure_layout(paint_state);

        /* Convert the measurements from mm to pixels */
        int inner_border = INNER_BORDER * paint_state->dpi * 10 / 254;

        painter->dialog_rect.x = painter->dialog_gap;
        painter->dialog_rect.y = painter->dialog_gap;
        painter->dialog_rect.w = paint_state->width - painter->dialog_gap * 2;

        int inner_width = painter->dialog_rect.w - inner_border * 2;

        struct vsx_layout *note_layout = painter->layouts[LAYOUT_NOTE].layout;

        vsx_layout_set_width(note_layout, inner_width);

        update_layout_text(painter);

        for (int i = 0; i < VSX_N_ELEMENTS(painter->layouts); i++)
                vsx_layout_prepare(painter->layouts[i].layout);

        const struct vsx_layout_extents *extents =
                vsx_layout_get_logical_extents(note_layout);

        painter->layouts[LAYOUT_NOTE].x =
                painter->dialog_rect.x + inner_border;
        painter->layouts[LAYOUT_NOTE].y =
                painter->dialog_rect.y + inner_border + extents->top;

        struct vsx_font_library *font_library = painter->toolbox->font_library;
        struct vsx_font *font = vsx_font_library_get_font(font_library, FONT);
        struct vsx_font_metrics font_metrics;

        vsx_font_get_metrics(font, &font_metrics);

        int name_y_pos = (painter->layouts[LAYOUT_NOTE].y -
                          font_metrics.ascender +
                          font_metrics.height * extents->n_lines);

        struct vsx_shell_interface *shell = painter->toolbox->shell;

        shell->set_name_position_cb(shell,
                                    name_y_pos,
                                    painter->dialog_rect.w -
                                    inner_border * 2);

        struct vsx_layout_paint_position *button_pos =
                painter->layouts + LAYOUT_BUTTON;
        const struct vsx_layout_extents *button_extents =
                vsx_layout_get_logical_extents(button_pos->layout);

        int name_height = shell->get_name_height_cb(shell);

        painter->button_rect.x = (painter->dialog_rect.x +
                                  painter->dialog_rect.w / 2 -
                                  button_extents->right / 2 -
                                  painter->button_border);
        painter->button_rect.y = (name_y_pos + name_height +
                                  font_metrics.height / 2);
        painter->button_rect.w = (button_extents->right +
                                  painter->button_border * 2);
        painter->button_rect.h = (font_metrics.height +
                                  painter->button_border * 2);

        button_pos->x = painter->button_rect.x + painter->button_border;
        button_pos->y = (painter->button_rect.y +
                         painter->button_border +
                         font_metrics.ascender);

        painter->dialog_rect.h = (painter->button_rect.y +
                                  painter->button_rect.h +
                                  inner_border -
                                  painter->dialog_rect.y);

        update_link_layouts(painter);

        update_transform(painter, paint_state);

        update_vertices(painter);

        create_dialog_shadow(painter);

        painter->layout_dirty = false;
}

static void
set_uniforms(struct vsx_name_painter *painter,
             const struct vsx_shader_data_program_data *program)
{
        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glUniformMatrix2fv(program->matrix_uniform,
                               1, /* count */
                               GL_FALSE, /* transpose */
                               painter->matrix);
        gl->glUniform2f(program->translation_uniform, -1.0f, 1.0f);

        gl->glUniform3f(program->color_uniform, 1.0f, 1.0f, 1.0f);
}

static void
paint_shadows(struct vsx_name_painter *painter)
{
        const struct vsx_paint_state *paint_state =
                &painter->toolbox->paint_state;

        GLfloat dialog_translation[] = {
                painter->dialog_rect.x * 2.0f / paint_state->width - 1.0f,
                -painter->dialog_rect.y * 2.0f / paint_state->height + 1.0f,
        };

        vsx_shadow_painter_paint(painter->toolbox->shadow_painter,
                                 painter->dialog_shadow,
                                 &painter->toolbox->shader_data,
                                 painter->matrix,
                                 dialog_translation);

        GLfloat links_translation[] = {
                painter->links_rect.x * 2.0f / paint_state->width - 1.0f,
                -painter->links_rect.y * 2.0f / paint_state->height + 1.0f,
        };

        vsx_shadow_painter_paint(painter->toolbox->shadow_painter,
                                 painter->links_shadow,
                                 &painter->toolbox->shader_data,
                                 painter->matrix,
                                 links_translation);
}

static void
paint_cb(void *painter_data)
{
        struct vsx_name_painter *painter = painter_data;

        if (!vsx_shadow_painter_is_ready(painter->toolbox->shadow_painter))
                return;

        paint_shadows(painter);

        const struct vsx_shader_data *shader_data =
                &painter->toolbox->shader_data;
        const struct vsx_shader_data_program_data *program =
                shader_data->programs + VSX_SHADER_DATA_PROGRAM_SOLID;

        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glUseProgram(program->program);

        set_uniforms(painter, program);

        vsx_array_object_bind(painter->vao, gl);

        vsx_gl_draw_range_elements(gl,
                                   GL_TRIANGLES,
                                   0, 2 * 4 - 1,
                                   2 * 6,
                                   painter->quad_buffer->type,
                                   NULL /* offset */);

        gl->glUniform3f(program->color_uniform, 0.498f, 0.523f, 0.781f);

        gl->glDrawArrays(GL_TRIANGLE_STRIP, 8, 4);

        struct vsx_layout_paint_params params = {
                .layouts = painter->layouts,
                .n_layouts = VSX_N_ELEMENTS(painter->layouts),
                .matrix = painter->matrix,
                .translation_x = -1.0f,
                .translation_y = 1.0f,
        };

        vsx_layout_paint_params(&params);
}

static void
open_privacy_policy(struct vsx_name_painter *painter)
{
        struct vsx_buffer link = VSX_BUFFER_STATIC_INIT;

        enum vsx_text_language language =
                vsx_game_state_get_language(painter->game_state);

        vsx_buffer_append_printf(&link,
                                 PRIVACY_POLICY_LINK_FORMAT,
                                 vsx_text_get(language,
                                              VSX_TEXT_LANGUAGE_CODE));

        struct vsx_shell_interface *shell = painter->toolbox->shell;

        shell->open_link_cb(shell,
                            (const char *) link.data,
                            painter->links_rect.x,
                            painter->links_rect.y,
                            painter->links_rect.w,
                            painter->links_rect.h);

        vsx_buffer_destroy(&link);
}

static bool
click_event_in_rect(const struct vsx_input_event *event,
                    const struct vsx_name_painter_rect *rect)
{
        return (event->click.x >= rect->x &&
                event->click.x < rect->x + rect->w &&
                event->click.y >= rect->y &&
                event->click.y < rect->y + rect->h);
}

static void
handle_click(struct vsx_name_painter *painter,
             const struct vsx_input_event *event)
{
        if (click_event_in_rect(event, &painter->button_rect)) {
                struct vsx_shell_interface *shell = painter->toolbox->shell;

                shell->request_name_cb(shell);
        } else if (click_event_in_rect(event, &painter->links_rect)) {
                if (event->click.y - painter->links_rect.y >
                    painter->links_rect.h / 2) {
                        vsx_game_state_set_dialog(painter->game_state,
                                                  VSX_DIALOG_COPYRIGHT);
                } else {
                        open_privacy_policy(painter);
                }
        }
}

static bool
input_event_cb(void *painter_data,
               const struct vsx_input_event *event)
{
        struct vsx_name_painter *painter = painter_data;

        switch (event->type) {
        case VSX_INPUT_EVENT_TYPE_DRAG_START:
        case VSX_INPUT_EVENT_TYPE_DRAG:
        case VSX_INPUT_EVENT_TYPE_ZOOM_START:
        case VSX_INPUT_EVENT_TYPE_ZOOM:
                /* Block all input until the player enters a name */
                return true;

        case VSX_INPUT_EVENT_TYPE_CLICK:
                handle_click(painter, event);
                return true;
        }

        return false;
}

static void
free_cb(void *painter_data)
{
        struct vsx_name_painter *painter = painter_data;

        vsx_list_remove(&painter->shadow_painter_ready_listener.link);
        vsx_list_remove(&painter->modified_listener.link);
        vsx_list_remove(&painter->name_size_listener.link);

        struct vsx_gl *gl = painter->toolbox->gl;

        if (painter->vao)
                vsx_array_object_free(painter->vao, gl);
        if (painter->vbo)
                gl->glDeleteBuffers(1, &painter->vbo);
        if (painter->quad_buffer)
                vsx_quad_tool_unref_buffer(painter->quad_buffer, gl);

        for (int i = 0; i < VSX_N_ELEMENTS(painter->layouts); i++) {
                if (painter->layouts[i].layout)
                        vsx_layout_free(painter->layouts[i].layout);
        }

        clear_dialog_shadow(painter);
        clear_links_shadow(painter);

        vsx_free(painter);
}

const struct vsx_painter
vsx_name_painter = {
        .create_cb = create_cb,
        .fb_size_changed_cb = fb_size_changed_cb,
        .prepare_cb = prepare_cb,
        .paint_cb = paint_cb,
        .input_event_cb = input_event_cb,
        .free_cb = free_cb,
};
