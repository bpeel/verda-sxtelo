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

#include "vsx-note-painter.h"

#include <stdbool.h>
#include <math.h>
#include <assert.h>

#include "vsx-gl.h"
#include "vsx-array-object.h"
#include "vsx-layout.h"
#include "vsx-main-thread.h"

struct vsx_note_painter {
        struct vsx_game_state *game_state;
        struct vsx_listener modified_listener;

        struct vsx_toolbox *toolbox;

        struct vsx_array_object *vao;
        GLuint vbo;

        int layout_x, layout_y;

        struct vsx_layout *layout;
        bool layout_dirty;

        struct vsx_signal redraw_needed_signal;

        char *text;
        struct vsx_main_thread_token *remove_note_timeout;
};

struct vertex {
        int16_t x, y;
};

#define N_VERTICES 4

/* Gap in mm between the bottom of the screen and the bottom of the note */
#define BOTTOM_GAP 5
/* Border around the note in mm */
#define BORDER 1
/* Maximum text width in mm */
#define TEXT_WIDTH 50

static void
cancel_timeout(struct vsx_note_painter *painter)
{
        if (painter->remove_note_timeout == NULL)
                return;

        vsx_main_thread_cancel_idle(painter->remove_note_timeout);
        painter->remove_note_timeout = NULL;
}

static void
remove_note(struct vsx_note_painter *painter)
{
        vsx_free(painter->text);
        painter->text = NULL;

        vsx_signal_emit(&painter->redraw_needed_signal, NULL);
}

static void
remove_note_cb(void *user_data)
{
        struct vsx_note_painter *painter = user_data;

        painter->remove_note_timeout = NULL;

        remove_note(painter);
}

static void
set_note_text(struct vsx_note_painter *painter,
              const char *text)
{
        vsx_free(painter->text);

        painter->text = vsx_strdup(text);
        painter->layout_dirty = true;

        cancel_timeout(painter);

        painter->remove_note_timeout =
                vsx_main_thread_queue_timeout(3 * 1000 * 1000,
                                              remove_note_cb,
                                              painter);

        vsx_signal_emit(&painter->redraw_needed_signal, NULL);
}

static void
modified_cb(struct vsx_listener *listener,
            void *user_data)
{
        struct vsx_note_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_note_painter,
                                 modified_listener);
        const struct vsx_game_state_modified_event *event = user_data;

        switch (event->type) {
        case VSX_GAME_STATE_MODIFIED_TYPE_NOTE:
                set_note_text(painter, event->note.text);
                break;
        default:
                break;
        }
}

static void
create_buffer(struct vsx_note_painter *painter)
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
                                       GL_SHORT,
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
        struct vsx_note_painter *painter = vsx_calloc(sizeof *painter);

        vsx_signal_init(&painter->redraw_needed_signal);

        painter->game_state = game_state;
        painter->toolbox = toolbox;

        painter->layout_dirty = true;

        painter->layout = vsx_layout_new(toolbox);
        vsx_layout_set_font(painter->layout,
                            VSX_FONT_TYPE_LABEL);

        create_buffer(painter);

        painter->modified_listener.notify = modified_cb;
        vsx_signal_add(vsx_game_state_get_modified_signal(game_state),
                       &painter->modified_listener);

        return painter;
}

static void
fb_size_changed_cb(void *painter_data)
{
        struct vsx_note_painter *painter = painter_data;

        painter->layout_dirty = true;
}

static void
prepare_cb(void *painter_data)
{
        struct vsx_note_painter *painter = painter_data;

        if (!painter->layout_dirty || painter->text == NULL)
                return;

        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        vsx_paint_state_ensure_layout(paint_state);

        /* Convert the measurements from mm to pixels */
        int bottom_gap = BOTTOM_GAP * paint_state->dpi * 10 / 254;
        int border = BORDER * paint_state->dpi * 10 / 254;
        int text_width = TEXT_WIDTH * paint_state->dpi * 10 / 254;

        if (text_width > paint_state->pixel_width - border * 2)
                text_width = paint_state->pixel_width - border * 2;

        vsx_layout_set_width(painter->layout, text_width);

        vsx_layout_set_text(painter->layout, painter->text);

        vsx_layout_prepare(painter->layout);

        const struct vsx_layout_extents *extents =
                vsx_layout_get_logical_extents(painter->layout);

        painter->layout_x = paint_state->pixel_width / 2 - extents->right / 2;
        painter->layout_y = (paint_state->pixel_height -
                             bottom_gap -
                             extents->bottom);

        int box_x1 = painter->layout_x - extents->left - border;
        int box_x2 = painter->layout_x + extents->right + border;
        int box_y1 = painter->layout_y - extents->top - border;
        int box_y2 = painter->layout_y + extents->bottom + border;

        struct vertex vertices[] = {
                { box_x1, box_y1 },
                { box_x1, box_y2 },
                { box_x2, box_y1 },
                { box_x2, box_y2 },
        };

        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);
        gl->glBufferData(GL_ARRAY_BUFFER,
                         sizeof vertices,
                         vertices,
                         GL_DYNAMIC_DRAW);

        painter->layout_dirty = false;
}

static void
paint_cb(void *painter_data)
{
        struct vsx_note_painter *painter = painter_data;

        if (painter->text == NULL)
                return;

        const struct vsx_shader_data *shader_data =
                &painter->toolbox->shader_data;
        const struct vsx_shader_data_program_data *program =
                shader_data->programs + VSX_SHADER_DATA_PROGRAM_SOLID;

        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glUseProgram(program->program);
        vsx_array_object_bind(painter->vao, gl);

        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        gl->glUniformMatrix2fv(program->matrix_uniform,
                               1, /* count */
                               GL_FALSE, /* transpose */
                               paint_state->pixel_matrix);
        gl->glUniform2f(program->translation_uniform,
                        paint_state->pixel_translation[0],
                        paint_state->pixel_translation[1]);
        gl->glUniform3f(program->color_uniform,
                        0.0f, 0.0f, 0.0f);

        gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        vsx_layout_paint(painter->layout,
                         painter->layout_x,
                         painter->layout_y,
                         1.0f, 1.0f, 1.0f);
}

static bool
input_event_cb(void *painter_data,
               const struct vsx_input_event *event)
{
        struct vsx_note_painter *painter = painter_data;

        switch (event->type) {
        case VSX_INPUT_EVENT_TYPE_DRAG:
        case VSX_INPUT_EVENT_TYPE_ZOOM:
                break;

        case VSX_INPUT_EVENT_TYPE_ZOOM_START:
        case VSX_INPUT_EVENT_TYPE_DRAG_START:
        case VSX_INPUT_EVENT_TYPE_CLICK:
                remove_note(painter);
                break;
        }

        return false;
}

static struct vsx_signal *
get_redraw_needed_signal_cb(void *painter_data)
{
        struct vsx_note_painter *painter = painter_data;

        return &painter->redraw_needed_signal;
}

static void
free_cb(void *painter_data)
{
        struct vsx_note_painter *painter = painter_data;

        vsx_list_remove(&painter->modified_listener.link);

        if (painter->layout)
                vsx_layout_free(painter->layout);

        vsx_free(painter->text);
        cancel_timeout(painter);

        struct vsx_gl *gl = painter->toolbox->gl;

        if (painter->vao)
                vsx_array_object_free(painter->vao, gl);
        if (painter->vbo)
                gl->glDeleteBuffers(1, &painter->vbo);

        vsx_free(painter);
}

const struct vsx_painter
vsx_note_painter = {
        .create_cb = create_cb,
        .fb_size_changed_cb = fb_size_changed_cb,
        .prepare_cb = prepare_cb,
        .paint_cb = paint_cb,
        .input_event_cb = input_event_cb,
        .get_redraw_needed_signal_cb = get_redraw_needed_signal_cb,
        .free_cb = free_cb,
};
