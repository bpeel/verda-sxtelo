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

#include "vsx-language-painter.h"

#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include <string.h>

#include "vsx-gl.h"
#include "vsx-array-object.h"
#include "vsx-util.h"
#include "vsx-layout.h"

struct language {
        const char *code;
        const char *name;
};

static const struct language
languages[] = {
        { .code = "en", .name = "English" },
        { .code = "fr", .name = "Français" },
        { .code = "eo", .name = "Esperanto" },
};

#define N_LANGUAGES VSX_N_ELEMENTS(languages)

struct language_button {
        struct vsx_layout *layout;
        int x;
};

struct vsx_language_painter {
        struct vsx_game_state *game_state;
        struct vsx_toolbox *toolbox;

        struct language_button buttons[N_LANGUAGES];

        int layout_y;
        int button_gap;
        int total_width, total_height;

        struct vsx_array_object *vao;
        GLuint vbo;

        struct vsx_shadow_painter_shadow *shadow;
        struct vsx_listener shadow_painter_ready_listener;
};

struct vertex {
        int16_t x, y;
};

#define N_VERTICES 4

/* Gap in mm between buttons */
#define BUTTON_GAP 5

/* Border in mm around all the buttons */
#define BORDER 4

static void
shadow_painter_ready_cb(struct vsx_listener *listener,
                        void *user_data)
{
        struct vsx_language_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_language_painter,
                                 shadow_painter_ready_listener);

        painter->toolbox->shell->queue_redraw_cb(painter->toolbox->shell);
}

static void
create_buttons(struct vsx_language_painter *painter)
{
        const struct vsx_paint_state *paint_state =
                &painter->toolbox->paint_state;

        /* Convert the button measurements from mm to pixels */
        painter->button_gap = BUTTON_GAP * paint_state->dpi * 10 / 254;
        int border = BORDER * paint_state->dpi * 10 / 254;

        int x = border;
        int max_top = 0, max_bottom = 0;

        for (int i = 0; i < N_LANGUAGES; i++) {
                struct language_button *button = painter->buttons + i;

                button->layout = vsx_layout_new(painter->toolbox);

                vsx_layout_set_text(button->layout, languages[i].name);
                vsx_layout_set_font(button->layout, VSX_FONT_TYPE_LABEL);

                vsx_layout_prepare(button->layout);

                const struct vsx_layout_extents *extents =
                        vsx_layout_get_logical_extents(button->layout);

                if (i > 0)
                        x += painter->button_gap;

                button->x = x;

                x += roundf(extents->right);

                if (extents->top > max_top)
                        max_top = extents->top;
                if (extents->bottom > max_bottom)
                        max_bottom = extents->bottom;
        }

        painter->layout_y = border + max_top;
        painter->total_width = x + border;
        painter->total_height = painter->layout_y + max_bottom + border;
}

static void
create_buffer(struct vsx_language_painter *painter)
{
        struct vertex vertices[N_VERTICES] = {
                { 0, 0 },
                { 0, painter->total_height },
                { painter->total_width, 0 },
                { painter->total_width, painter->total_height },
        };

        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glGenBuffers(1, &painter->vbo);
        gl->glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);
        gl->glBufferData(GL_ARRAY_BUFFER,
                         N_VERTICES * sizeof (struct vertex),
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

static void *
create_cb(struct vsx_game_state *game_state,
          struct vsx_toolbox *toolbox)
{
        struct vsx_language_painter *painter = vsx_calloc(sizeof *painter);

        painter->game_state = game_state;
        painter->toolbox = toolbox;

        create_buttons(painter);
        create_buffer(painter);

        struct vsx_shadow_painter *shadow_painter = toolbox->shadow_painter;

        painter->shadow =
                vsx_shadow_painter_create_shadow(shadow_painter,
                                                 painter->total_width,
                                                 painter->total_height);

        painter->shadow_painter_ready_listener.notify =
                shadow_painter_ready_cb;
        vsx_signal_add(vsx_shadow_painter_get_ready_signal(shadow_painter),
                       &painter->shadow_painter_ready_listener);

        return painter;
}

static void
get_origin(struct vsx_language_painter *painter,
           int *origin_x,
           int *origin_y)
{
        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        vsx_paint_state_ensure_layout(paint_state);

        *origin_x = paint_state->pixel_width / 2 - painter->total_width / 2;
        *origin_y = paint_state->pixel_height / 2 - painter->total_height / 2;
}

static bool
handle_click(struct vsx_language_painter *painter,
             const struct vsx_input_event *event)
{
        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        int x, y;

        vsx_paint_state_screen_to_pixel(paint_state,
                                        event->click.x,
                                        event->click.y,
                                        &x, &y);

        int origin_x, origin_y;

        get_origin(painter, &origin_x, &origin_y);

        x -= origin_x;
        y -= origin_y;

        if (x < 0 || x >= painter->total_width ||
            y < 0 || y >= painter->total_height) {
                vsx_game_state_close_dialog(painter->game_state);
                return true;
        }

        int language_num;

        for (language_num = 0; language_num < N_LANGUAGES - 1; language_num++) {
                if (x < (painter->buttons[language_num + 1].x -
                         painter->button_gap / 2))
                        break;
        }

        vsx_game_state_set_language(painter->game_state,
                                    languages[language_num].code);
        vsx_game_state_set_dialog(painter->game_state,
                                  VSX_DIALOG_MENU);

        return true;
}

static bool
input_event_cb(void *painter_data,
               const struct vsx_input_event *event)
{
        struct vsx_language_painter *painter = painter_data;

        switch (event->type) {
        case VSX_INPUT_EVENT_TYPE_DRAG_START:
        case VSX_INPUT_EVENT_TYPE_DRAG:
        case VSX_INPUT_EVENT_TYPE_ZOOM_START:
        case VSX_INPUT_EVENT_TYPE_ZOOM:
                return true;

        case VSX_INPUT_EVENT_TYPE_CLICK:
                return handle_click(painter, event);
        }

        return false;
}

static void
paint_shadow(struct vsx_language_painter *painter,
             int x_off,
             int y_off)
{
        struct vsx_paint_state *paint_state =
                &painter->toolbox->paint_state;

        GLfloat translation[2];

        vsx_paint_state_offset_pixel_translation(paint_state,
                                                 x_off, y_off,
                                                 translation);

        vsx_shadow_painter_paint(painter->toolbox->shadow_painter,
                                 painter->shadow,
                                 &painter->toolbox->shader_data,
                                 paint_state->pixel_matrix,
                                 translation);
}

static void
update_uniforms(struct vsx_language_painter *painter,
                const struct vsx_shader_data_program_data *program)
{
        struct vsx_paint_state *paint_state =
                &painter->toolbox->paint_state;

        float translation[2];

        int origin_x = (paint_state->pixel_width / 2 -
                        painter->total_width / 2);
        int origin_y = (paint_state->pixel_height / 2 -
                        painter->total_height / 2);

        vsx_paint_state_offset_pixel_translation(paint_state,
                                                 origin_x, origin_y,
                                                 translation);

        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glUniformMatrix2fv(program->matrix_uniform,
                               1, /* count */
                               GL_FALSE, /* transpose */
                               paint_state->pixel_matrix);
        gl->glUniform2f(program->translation_uniform,
                        translation[0],
                        translation[1]);

        gl->glUniform3f(program->color_uniform, 1.0f, 1.0f, 1.0f);
}

static void
paint_cb(void *painter_data)
{
        struct vsx_language_painter *painter = painter_data;

        if (!vsx_shadow_painter_is_ready(painter->toolbox->shadow_painter))
                return;

        int x_off, y_off;

        get_origin(painter, &x_off, &y_off);

        paint_shadow(painter, x_off, y_off);

        const struct vsx_shader_data *shader_data =
                &painter->toolbox->shader_data;
        const struct vsx_shader_data_program_data *program =
                shader_data->programs + VSX_SHADER_DATA_PROGRAM_SOLID;

        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glUseProgram(program->program);

        update_uniforms(painter, program);

        vsx_array_object_bind(painter->vao, gl);

        gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, N_VERTICES);

        struct vsx_layout_paint_position pos[N_LANGUAGES];

        for (int i = 0; i < N_LANGUAGES; i++) {
                pos[i].layout = painter->buttons[i].layout;
                pos[i].x = painter->buttons[i].x + x_off;
                pos[i].y = painter->layout_y + y_off;
                pos[i].r = 0.0f;
                pos[i].g = 0.0f;
                pos[i].b = 0.0f;
        }

        vsx_layout_paint_multiple(pos, N_LANGUAGES);
}

static void
free_cb(void *painter_data)
{
        struct vsx_language_painter *painter = painter_data;

        vsx_list_remove(&painter->shadow_painter_ready_listener.link);

        for (int i = 0; i < VSX_N_ELEMENTS(painter->buttons); i++)
                vsx_layout_free(painter->buttons[i].layout);

        struct vsx_gl *gl = painter->toolbox->gl;

        if (painter->vao)
                vsx_array_object_free(painter->vao, gl);
        if (painter->vbo)
                gl->glDeleteBuffers(1, &painter->vbo);

        if (painter->shadow) {
                vsx_shadow_painter_free_shadow(painter->toolbox->shadow_painter,
                                               painter->shadow);
        }

        vsx_free(painter);
}

const struct vsx_painter
vsx_language_painter = {
        .create_cb = create_cb,
        .paint_cb = paint_cb,
        .input_event_cb = input_event_cb,
        .free_cb = free_cb,
};
