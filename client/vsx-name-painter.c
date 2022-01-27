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

#include "vsx-map-buffer.h"
#include "vsx-gl.h"
#include "vsx-array-object.h"
#include "vsx-layout.h"

struct vsx_name_painter {
        struct vsx_game_state *game_state;
        struct vsx_listener modified_listener;

        struct vsx_toolbox *toolbox;

        struct vsx_array_object *vao;
        GLuint vbo;

        bool layout_dirty;

        struct vsx_layout *layout;

        int dialog_x, dialog_y;
        int dialog_width, dialog_height;

        int layout_x, layout_y;

        /* This is using its own pixel transformation because we don’t
         * want to take into account the board rotation.
         */
        GLfloat matrix[4];

        struct vsx_signal redraw_needed_signal;
};

struct vertex {
        float x, y;
};

#define N_QUADS 1
#define N_VERTICES (N_QUADS * 4)

/* Gap in MM around the dialog */
#define DIALOG_GAP 5
/* Border in MM inside the dialog around the contents */
#define INNER_BORDER 5

#define FONT VSX_FONT_TYPE_LABEL

static void
modified_cb(struct vsx_listener *listener,
            void *user_data)
{
        struct vsx_name_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_name_painter,
                                 modified_listener);
        const struct vsx_game_state_modified_event *event = user_data;

        switch (event->type) {
        case VSX_GAME_STATE_MODIFIED_TYPE_LANGUAGE:
        case VSX_GAME_STATE_MODIFIED_TYPE_NAME_HEIGHT:
                painter->layout_dirty = true;
                vsx_signal_emit(&painter->redraw_needed_signal, NULL);
                break;
        default:
                break;
        }
}

static void
update_vertices(struct vsx_name_painter *painter)
{
        struct vertex vertices[N_VERTICES];

        struct vertex *v = vertices;

        v->x = painter->dialog_x;
        v->y = painter->dialog_y;
        v++;

        v->x = painter->dialog_x;
        v->y = painter->dialog_y + painter->dialog_height;
        v++;

        v->x = painter->dialog_x + painter->dialog_width;
        v->y = painter->dialog_y;
        v++;

        v->x = painter->dialog_x + painter->dialog_width;
        v->y = painter->dialog_y + painter->dialog_height;
        v++;

        vsx_gl.glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);
        vsx_gl.glBufferData(GL_ARRAY_BUFFER,
                            sizeof vertices,
                            vertices,
                            GL_DYNAMIC_DRAW);
}

static void
create_buffer(struct vsx_name_painter *painter)
{
        struct vsx_gl *gl = painter->toolbox->gl;

        vsx_gl.glGenBuffers(1, &painter->vbo);
        vsx_gl.glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);
        vsx_gl.glBufferData(GL_ARRAY_BUFFER,
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
                                       0, /* divisor */
                                       painter->vbo,
                                       offsetof(struct vertex, x));
}

static void *
create_cb(struct vsx_game_state *game_state,
          struct vsx_toolbox *toolbox)
{
        struct vsx_name_painter *painter = vsx_calloc(sizeof *painter);

        vsx_signal_init(&painter->redraw_needed_signal);

        painter->game_state = game_state;
        painter->toolbox = toolbox;
        painter->layout_dirty = true;

        create_buffer(painter);
        update_vertices(painter);

        painter->layout = vsx_layout_new(toolbox);
        vsx_layout_set_font(painter->layout, FONT);

        painter->modified_listener.notify = modified_cb;
        vsx_signal_add(vsx_game_state_get_modified_signal(game_state),
                       &painter->modified_listener);

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
prepare_cb(void *painter_data)
{
        struct vsx_name_painter *painter = painter_data;

        if (!painter->layout_dirty)
                return;

        struct vsx_paint_state *paint_state =
                &painter->toolbox->paint_state;
        vsx_paint_state_ensure_layout(paint_state);

        /* Convert the measurements from mm to pixels */
        int dialog_gap = DIALOG_GAP * paint_state->dpi * 10 / 254;
        int inner_border = INNER_BORDER * paint_state->dpi * 10 / 254;

        painter->dialog_x = dialog_gap;
        painter->dialog_y = dialog_gap;
        painter->dialog_width = paint_state->width - dialog_gap * 2;

        int inner_width = painter->dialog_width - inner_border * 2;

        vsx_layout_set_width(painter->layout, inner_width);

        enum vsx_text_language language =
                vsx_game_state_get_language(painter->game_state);
        enum vsx_text note =
                vsx_game_state_get_name_note(painter->game_state);
        vsx_layout_set_text(painter->layout, vsx_text_get(language, note));

        vsx_layout_prepare(painter->layout);

        const struct vsx_layout_extents *extents =
                vsx_layout_get_logical_extents(painter->layout);

        painter->layout_x = painter->dialog_x + inner_border;
        painter->layout_y = painter->dialog_y + inner_border + extents->top;

        struct vsx_font_library *font_library = painter->toolbox->font_library;
        struct vsx_font *font = vsx_font_library_get_font(font_library, FONT);
        struct vsx_font_metrics font_metrics;

        vsx_font_get_metrics(font, &font_metrics);

        int name_y_pos = (painter->layout_y -
                          font_metrics.ascender +
                          font_metrics.height * extents->n_lines);

        vsx_game_state_set_name_position(painter->game_state,
                                         name_y_pos,
                                         painter->dialog_width -
                                         inner_border * 2);

        int name_height = vsx_game_state_get_name_height(painter->game_state);

        painter->dialog_height = (name_y_pos -
                                  painter->dialog_y +
                                  name_height +
                                  inner_border);

        update_transform(painter, paint_state);

        update_vertices(painter);

        painter->layout_dirty = false;
}

static void
set_uniforms(struct vsx_name_painter *painter,
             const struct vsx_shader_data_program_data *program)
{
        vsx_gl.glUniformMatrix2fv(program->matrix_uniform,
                                  1, /* count */
                                  GL_FALSE, /* transpose */
                                  painter->matrix);
        vsx_gl.glUniform2f(program->translation_uniform, -1.0f, 1.0f);

        vsx_gl.glUniform3f(program->color_uniform, 1.0f, 1.0f, 1.0f);
}

static void
paint_cb(void *painter_data)
{
        struct vsx_name_painter *painter = painter_data;

        const struct vsx_shader_data *shader_data =
                &painter->toolbox->shader_data;
        const struct vsx_shader_data_program_data *program =
                shader_data->programs + VSX_SHADER_DATA_PROGRAM_SOLID;

        struct vsx_gl *gl = painter->toolbox->gl;

        vsx_gl.glUseProgram(program->program);

        set_uniforms(painter, program);

        vsx_array_object_bind(painter->vao, gl);

        vsx_gl.glDrawArrays(GL_TRIANGLE_STRIP, 0, N_VERTICES);

        struct vsx_layout_paint_position pos = {
                .layout = painter->layout,
                .x = painter->layout_x,
                .y = painter->layout_y,
        };

        struct vsx_layout_paint_params params = {
                .layouts = &pos,
                .n_layouts = 1,
                .matrix = painter->matrix,
                .translation_x = -1.0f,
                .translation_y = 1.0f,
        };

        vsx_layout_paint_params(&params);
}

static struct vsx_signal *
get_redraw_needed_signal_cb(void *painter_data)
{
        struct vsx_name_painter *painter = painter_data;

        return &painter->redraw_needed_signal;
}

static bool
input_event_cb(void *painter_data,
               const struct vsx_input_event *event)
{
        switch (event->type) {
        case VSX_INPUT_EVENT_TYPE_DRAG_START:
        case VSX_INPUT_EVENT_TYPE_DRAG:
        case VSX_INPUT_EVENT_TYPE_ZOOM_START:
        case VSX_INPUT_EVENT_TYPE_ZOOM:
        case VSX_INPUT_EVENT_TYPE_CLICK:
                /* Block all input until the player enters a name */
                return true;
        }

        return false;
}

static void
free_cb(void *painter_data)
{
        struct vsx_name_painter *painter = painter_data;

        vsx_list_remove(&painter->modified_listener.link);

        struct vsx_gl *gl = painter->toolbox->gl;

        if (painter->vao)
                vsx_array_object_free(painter->vao, gl);
        if (painter->vbo)
                vsx_gl.glDeleteBuffers(1, &painter->vbo);

        if (painter->layout)
                vsx_layout_free(painter->layout);

        vsx_free(painter);
}

const struct vsx_painter
vsx_name_painter = {
        .create_cb = create_cb,
        .fb_size_changed_cb = fb_size_changed_cb,
        .prepare_cb = prepare_cb,
        .paint_cb = paint_cb,
        .input_event_cb = input_event_cb,
        .get_redraw_needed_signal_cb = get_redraw_needed_signal_cb,
        .free_cb = free_cb,
};
