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

#include "vsx-menu-painter.h"

#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "vsx-map-buffer.h"
#include "vsx-mipmap.h"
#include "vsx-gl.h"
#include "vsx-array-object.h"
#include "vsx-layout.h"

#define N_BUTTONS 5

struct vsx_menu_painter {
        struct vsx_game_state *game_state;
        struct vsx_listener modified_listener;
        struct vsx_toolbox *toolbox;

        bool layout_dirty;
        GLfloat translation[2];
        int button_size;
        int border;
        int dialog_x, dialog_y;
        int dialog_height;

        struct vsx_layout_paint_position labels[N_BUTTONS];

        bool vertices_dirty;

        struct vsx_array_object *vao;
        GLuint vbo;
        struct vsx_quad_tool_buffer *quad_buffer;

        GLuint tex;
        struct vsx_image_loader_token *image_token;

        struct vsx_shadow_painter_shadow *shadow;
        struct vsx_listener shadow_painter_ready_listener;

        struct vsx_signal redraw_needed_signal;
};

struct vertex {
        int16_t x, y;
        float s, t;
};

enum menu_button {
        MENU_BUTTON_LANGUAGE,
        MENU_BUTTON_SHARE,
        MENU_BUTTON_LENGTH,
        MENU_BUTTON_HELP,
        MENU_BUTTON_LEAVE,
};

enum menu_image {
        MENU_IMAGE_LANGUAGE,
        MENU_IMAGE_SHARE,
        MENU_IMAGE_SHORT_GAME,
        MENU_IMAGE_LONG_GAME,
        MENU_IMAGE_HELP,
        MENU_IMAGE_LEAVE,
};

/* Each button is extended up and down to draw the border. There are
 * an additonal two quads to draw the left and right border.
 */
#define N_QUADS (N_BUTTONS + 2)
#define N_VERTICES (N_QUADS * 4)

#define N_IMAGES 8

/* Size in mm of a button */
#define BUTTON_SIZE 15

/* Border in mm around all the buttons */
#define BORDER 4

#define SHORT_GAME_N_TILES 50
#define LONG_GAME_N_TILES 122
/* If the number of tiles is at least this then we’ll assume it’s a
 * long game.
 */
#define LONG_GAME_THRESHOLD ((SHORT_GAME_N_TILES + LONG_GAME_N_TILES) / 2)

static bool
is_long_game(struct vsx_menu_painter *painter)
{
        return (vsx_game_state_get_n_tiles(painter->game_state) >=
                LONG_GAME_THRESHOLD);
}

static bool
painter_is_ready(struct vsx_menu_painter *painter)
{
        return (painter->tex &&
                vsx_shadow_painter_is_ready(painter->toolbox->shadow_painter));
}

static void
modified_cb(struct vsx_listener *listener,
            void *user_data)
{
        struct vsx_menu_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_menu_painter,
                                 modified_listener);
        const struct vsx_game_state_modified_event *event = user_data;

        switch (event->type) {
        case VSX_GAME_STATE_MODIFIED_TYPE_LANGUAGE:
        case VSX_GAME_STATE_MODIFIED_TYPE_N_TILES:
                painter->layout_dirty = true;
                vsx_signal_emit(&painter->redraw_needed_signal, NULL);
                break;

        default:
                break;
        }
}

static void
shadow_painter_ready_cb(struct vsx_listener *listener,
                        void *user_data)
{
        struct vsx_menu_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_menu_painter,
                                 shadow_painter_ready_listener);

        if (painter_is_ready(painter))
                vsx_signal_emit(&painter->redraw_needed_signal, NULL);
}

static void
clear_shadow(struct vsx_menu_painter *painter)
{
        if (painter->shadow == NULL)
                return;

        vsx_shadow_painter_free_shadow(painter->toolbox->shadow_painter,
                                       painter->shadow);
        painter->shadow = NULL;
}

static void
create_shadow(struct vsx_menu_painter *painter)
{
        clear_shadow(painter);

        struct vsx_shadow_painter *shadow_painter =
                painter->toolbox->shadow_painter;

        int w = painter->border * 2 + N_BUTTONS * painter->button_size;
        int h = painter->dialog_height;

        painter->shadow =
                vsx_shadow_painter_create_shadow(shadow_painter, w, h);
}

static void
update_label_text(struct vsx_menu_painter *painter)
{
        enum vsx_text_language language =
                vsx_game_state_get_language(painter->game_state);

        int bottom_most = 0;

        for (int i = 0; i < N_BUTTONS; i++) {
                struct vsx_layout *layout = painter->labels[i].layout;

                vsx_layout_set_width(layout, painter->button_size);

                enum vsx_text text = 0;

                switch ((enum menu_button) i) {
                case MENU_BUTTON_LANGUAGE:
                        text = VSX_TEXT_LANGUAGE_BUTTON;
                        break;
                case MENU_BUTTON_SHARE:
                        text = VSX_TEXT_SHARE_BUTTON;
                        break;
                case MENU_BUTTON_LENGTH:
                        text = (is_long_game(painter) ?
                                VSX_TEXT_LONG_GAME :
                                VSX_TEXT_SHORT_GAME);
                        break;
                case MENU_BUTTON_HELP:
                        text = VSX_TEXT_HELP_BUTTON;
                        break;
                case MENU_BUTTON_LEAVE:
                        text = VSX_TEXT_LEAVE_BUTTON;
                        break;
                }

                vsx_layout_set_text(layout, vsx_text_get(language, text));

                vsx_layout_prepare(layout);

                const struct vsx_layout_extents *extents =
                        vsx_layout_get_logical_extents(layout);

                int bottom = extents->top + extents->bottom;

                if (bottom > bottom_most)
                        bottom_most = bottom;
        }

        painter->dialog_height = (painter->button_size +
                                  bottom_most +
                                  painter->border * 2);
}

static void
update_label_positions(struct vsx_menu_painter *painter)
{
        for (int i = 0; i < N_BUTTONS; i++) {
                struct vsx_layout *layout = painter->labels[i].layout;

                const struct vsx_layout_extents *extents =
                        vsx_layout_get_logical_extents(layout);

                painter->labels[i].x =
                        painter->dialog_x +
                        painter->border +
                        i * painter->button_size +
                        painter->button_size / 2 -
                        extents->right / 2;
                painter->labels[i].y =
                        painter->dialog_y +
                        painter->border +
                        painter->button_size +
                        extents->top;
        }
}

static void
ensure_layout(struct vsx_menu_painter *painter)
{
        if (!painter->layout_dirty)
                return;

        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        vsx_paint_state_ensure_layout(paint_state);

        /* Convert the measurements from mm to pixels */
        painter->button_size = BUTTON_SIZE * paint_state->dpi * 10 / 254;
        painter->border = BORDER * paint_state->dpi * 10 / 254;

        int button_space = paint_state->pixel_width - painter->border * 2;

        if (painter->button_size * N_BUTTONS > button_space)
                painter->button_size = button_space / N_BUTTONS;

        update_label_text(painter);

        painter->dialog_x = (paint_state->pixel_width / 2 -
                             (painter->button_size * N_BUTTONS +
                              painter->border * 2) /
                             2);
        painter->dialog_y = (paint_state->pixel_height / 2 -
                             painter->dialog_height / 2);

        vsx_paint_state_offset_pixel_translation(paint_state,
                                                 painter->dialog_x,
                                                 painter->dialog_y,
                                                 painter->translation);

        update_label_positions(painter);

        create_shadow(painter);

        painter->vertices_dirty = true;
        painter->layout_dirty = false;
}

static void
texture_load_cb(const struct vsx_image *image,
                struct vsx_error *error,
                void *data)
{
        struct vsx_menu_painter *painter = data;

        painter->image_token = NULL;

        if (error) {
                fprintf(stderr,
                        "error loading menu image: %s\n",
                        error->message);
                return;
        }

        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glGenTextures(1, &painter->tex);

        gl->glBindTexture(GL_TEXTURE_2D, painter->tex);
        gl->glTexParameteri(GL_TEXTURE_2D,
                            GL_TEXTURE_WRAP_S,
                            GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D,
                            GL_TEXTURE_WRAP_T,
                            GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D,
                            GL_TEXTURE_MIN_FILTER,
                            GL_LINEAR_MIPMAP_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D,
                            GL_TEXTURE_MAG_FILTER,
                            GL_LINEAR);

        vsx_mipmap_load_image(image, gl, painter->tex);

        if (painter_is_ready(painter))
                vsx_signal_emit(&painter->redraw_needed_signal, NULL);
}

static void
create_buffer(struct vsx_menu_painter *painter)
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
        vsx_array_object_set_attribute(painter->vao,
                                       gl,
                                       VSX_SHADER_DATA_ATTRIB_TEX_COORD,
                                       2, /* size */
                                       GL_FLOAT,
                                       false, /* normalized */
                                       sizeof (struct vertex),
                                       0, /* divisor */
                                       painter->vbo,
                                       offsetof(struct vertex, s));

        painter->quad_buffer =
                vsx_quad_tool_get_buffer(painter->toolbox->quad_tool,
                                         painter->vao,
                                         N_QUADS);
}

static void *
create_cb(struct vsx_game_state *game_state,
          struct vsx_toolbox *toolbox)
{
        struct vsx_menu_painter *painter = vsx_calloc(sizeof *painter);

        painter->layout_dirty = true;
        painter->vertices_dirty = true;
        painter->game_state = game_state;
        painter->toolbox = toolbox;

        vsx_signal_init(&painter->redraw_needed_signal);

        create_buffer(painter);

        painter->modified_listener.notify = modified_cb;
        vsx_signal_add(vsx_game_state_get_modified_signal(game_state),
                       &painter->modified_listener);

        painter->shadow_painter_ready_listener.notify =
                shadow_painter_ready_cb;
        struct vsx_shadow_painter *shadow_painter = toolbox->shadow_painter;
        vsx_signal_add(vsx_shadow_painter_get_ready_signal(shadow_painter),
                       &painter->shadow_painter_ready_listener);

        for (int i = 0; i < N_BUTTONS; i++) {
                struct vsx_layout_paint_position *label =
                        painter->labels + i;
                label->layout = vsx_layout_new(toolbox);
                label->r = 0.0f;
                label->g = 0.0f;
                label->b = 0.0f;
        }

        struct vsx_image_loader *image_loader =
                painter->toolbox->image_loader;

        painter->image_token = vsx_image_loader_load(image_loader,
                                                     "menu.mpng",
                                                     texture_load_cb,
                                                     painter);

        return painter;
}

static void
fb_size_changed_cb(void *painter_data)
{
        struct vsx_menu_painter *painter = painter_data;

        painter->layout_dirty = true;
}

static void
handle_language_button(struct vsx_menu_painter *painter)
{
        if (vsx_game_state_get_started(painter->game_state)) {
                enum vsx_text_language language =
                        vsx_game_state_get_language(painter->game_state);
                const char *note =
                        vsx_text_get(language,
                                     VSX_TEXT_CANT_CHANGE_LANGUAGE_STARTED);
                vsx_game_state_set_note(painter->game_state, note);
        } else {
                vsx_game_state_set_dialog(painter->game_state,
                                          VSX_DIALOG_LANGUAGE);
        }
}

static void
handle_toggle_length(struct vsx_menu_painter *painter)
{
        if (vsx_game_state_get_started(painter->game_state)) {
                enum vsx_text_language language =
                        vsx_game_state_get_language(painter->game_state);
                const char *note =
                        vsx_text_get(language,
                                     VSX_TEXT_CANT_CHANGE_LENGTH_STARTED);
                vsx_game_state_set_note(painter->game_state, note);
        } else {
                int n_tiles = (is_long_game(painter) ?
                               SHORT_GAME_N_TILES :
                               LONG_GAME_N_TILES);
                vsx_game_state_set_n_tiles(painter->game_state, n_tiles);
        }
}

static void
handle_help_button(struct vsx_menu_painter *painter)
{
        vsx_game_state_set_page(painter->game_state, 0);
        vsx_game_state_set_dialog(painter->game_state, VSX_DIALOG_GUIDE);
}

static bool
handle_click(struct vsx_menu_painter *painter,
             const struct vsx_input_event *event)
{
        ensure_layout(painter);

        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        int x, y;

        vsx_paint_state_screen_to_pixel(paint_state,
                                        event->click.x,
                                        event->click.y,
                                        &x, &y);

        x -= painter->dialog_x + painter->border;
        y -= painter->dialog_y + painter->border;

        if (x < 0 || x >= painter->button_size * N_BUTTONS ||
            y < 0 || y >= painter->dialog_height - painter->border * 2) {
                vsx_game_state_set_dialog(painter->game_state,
                                          VSX_DIALOG_NONE);
                return true;
        }

        switch ((enum menu_button) (x / painter->button_size)) {
        case MENU_BUTTON_LANGUAGE:
                handle_language_button(painter);
                break;
        case MENU_BUTTON_SHARE:
                vsx_game_state_set_dialog(painter->game_state,
                                          VSX_DIALOG_INVITE_LINK);
                break;
        case MENU_BUTTON_LENGTH:
                handle_toggle_length(painter);
                break;
        case MENU_BUTTON_HELP:
                handle_help_button(painter);
                break;
        case MENU_BUTTON_LEAVE:
                vsx_game_state_leave(painter->game_state);
                break;
        }

        return true;
}

static bool
input_event_cb(void *painter_data,
               const struct vsx_input_event *event)
{
        struct vsx_menu_painter *painter = painter_data;

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
store_quad(struct vertex *vertices,
           int x, int y,
           int w, int h,
           float s1, float t1,
           float s2, float t2)
{
        struct vertex *v = vertices;

        v->x = x;
        v->y = y;
        v->s = s1;
        v->t = t1;
        v++;
        v->x = x;
        v->y = y + h;
        v->s = s1;
        v->t = t2;
        v++;
        v->x = x + w;
        v->y = y;
        v->s = s2;
        v->t = t1;
        v++;
        v->x = x + w;
        v->y = y + h;
        v->s = s2;
        v->t = t2;
}

static void
ensure_vertices(struct vsx_menu_painter *painter)
{
        if (!painter->vertices_dirty)
                return;

        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);

        struct vertex *vertices =
                vsx_map_buffer_map(painter->toolbox->map_buffer,
                                   GL_ARRAY_BUFFER,
                                   N_VERTICES * sizeof (struct vertex),
                                   false, /* flush explicit */
                                   GL_DYNAMIC_DRAW);

        struct vertex *v = vertices;

        for (int i = 0; i < N_BUTTONS; i++) {
                enum menu_image image = 0;

                switch ((enum menu_button) i) {
                case MENU_BUTTON_LANGUAGE:
                        image = MENU_IMAGE_LANGUAGE;
                        break;
                case MENU_BUTTON_SHARE:
                        image = MENU_IMAGE_SHARE;
                        break;
                case MENU_BUTTON_LENGTH:
                        image = (is_long_game(painter) ?
                                 MENU_IMAGE_LONG_GAME :
                                 MENU_IMAGE_SHORT_GAME);
                        break;
                case MENU_BUTTON_HELP:
                        image = MENU_IMAGE_HELP;
                        break;
                case MENU_BUTTON_LEAVE:
                        image = MENU_IMAGE_LEAVE;
                        break;
                }

                /* Button image. The image is extended to paint the
                 * area above and below as well.
                 */
                store_quad(v,
                           painter->border + i * painter->button_size,
                           0, /* y */
                           painter->button_size,
                           painter->dialog_height,
                           image / (float) N_IMAGES,
                           -painter->border / (float) painter->button_size,
                           (image + 1.0f) / N_IMAGES,
                           (painter->dialog_height - painter->border) /
                           (float) painter->button_size);
                v += 4;
        }

        /* Side borders */
        store_quad(v,
                   0, 0, /* x/y */
                   painter->border,
                   painter->dialog_height,
                   0.0f, 0.0f, 0.0f, 0.0f);
        v += 4;

        store_quad(v,
                   painter->border + N_BUTTONS * painter->button_size,
                   0, /* x/y */
                   painter->border,
                   painter->dialog_height,
                   0.0f, 0.0f, 0.0f, 0.0f);
        v += 4;

        vsx_map_buffer_unmap(painter->toolbox->map_buffer);

        assert(v - vertices == N_VERTICES);

        painter->vertices_dirty = false;
}

static void
prepare_cb(void *painter_data)
{
        struct vsx_menu_painter *painter = painter_data;

        if (painter->tex == 0)
                return;

        ensure_layout(painter);
}

static void
paint_cb(void *painter_data)
{
        struct vsx_menu_painter *painter = painter_data;

        if (!painter_is_ready(painter))
                return;

        vsx_shadow_painter_paint(painter->toolbox->shadow_painter,
                                 painter->shadow,
                                 &painter->toolbox->shader_data,
                                 painter->toolbox->paint_state.pixel_matrix,
                                 painter->translation);

        ensure_vertices(painter);

        const struct vsx_shader_data *shader_data =
                &painter->toolbox->shader_data;
        const struct vsx_shader_data_program_data *program =
                shader_data->programs + VSX_SHADER_DATA_PROGRAM_TEXTURE;

        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glUseProgram(program->program);

        gl->glUniformMatrix2fv(program->matrix_uniform,
                               1, /* count */
                               GL_FALSE, /* transpose */
                               painter->toolbox->paint_state.pixel_matrix);
        gl->glUniform2f(program->translation_uniform,
                        painter->translation[0],
                        painter->translation[1]);

        vsx_array_object_bind(painter->vao, gl);

        gl->glBindTexture(GL_TEXTURE_2D, painter->tex);

        vsx_gl_draw_range_elements(gl,
                                   GL_TRIANGLES,
                                   0, N_VERTICES - 1,
                                   N_QUADS * 6,
                                   painter->quad_buffer->type,
                                   NULL /* indices */);

        vsx_layout_paint_multiple(painter->labels, N_BUTTONS);
}

static struct vsx_signal *
get_redraw_needed_signal_cb(void *painter_data)
{
        struct vsx_menu_painter *painter = painter_data;

        return &painter->redraw_needed_signal;
}

static void
free_cb(void *painter_data)
{
        struct vsx_menu_painter *painter = painter_data;

        vsx_list_remove(&painter->shadow_painter_ready_listener.link);
        vsx_list_remove(&painter->modified_listener.link);

        for (int i = 0; i < N_BUTTONS; i++)
                vsx_layout_free(painter->labels[i].layout);

        clear_shadow(painter);

        struct vsx_gl *gl = painter->toolbox->gl;

        if (painter->vao)
                vsx_array_object_free(painter->vao, gl);
        if (painter->vbo)
                gl->glDeleteBuffers(1, &painter->vbo);
        if (painter->quad_buffer)
                vsx_quad_tool_unref_buffer(painter->quad_buffer, gl);

        if (painter->image_token)
                vsx_image_loader_cancel(painter->image_token);
        if (painter->tex)
                gl->glDeleteTextures(1, &painter->tex);

        vsx_free(painter);
}

const struct vsx_painter
vsx_menu_painter = {
        .create_cb = create_cb,
        .fb_size_changed_cb = fb_size_changed_cb,
        .prepare_cb = prepare_cb,
        .paint_cb = paint_cb,
        .input_event_cb = input_event_cb,
        .get_redraw_needed_signal_cb = get_redraw_needed_signal_cb,
        .free_cb = free_cb,
};
