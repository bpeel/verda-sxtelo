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
#include "vsx-quad-buffer.h"

struct vsx_menu_painter {
        struct vsx_game_state *game_state;
        struct vsx_listener modified_listener;
        struct vsx_painter_toolbox *toolbox;

        bool layout_dirty;
        GLfloat matrix[4];
        GLfloat translation[2];
        int button_size;

        bool vertices_dirty;

        GLuint program;
        GLint matrix_uniform;
        GLint translation_uniform;

        struct vsx_array_object *vao;
        GLuint vbo;
        GLuint element_buffer;

        GLuint tex;
        struct vsx_image_loader_token *image_token;

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
};

enum menu_image {
        MENU_IMAGE_LANGUAGE,
        MENU_IMAGE_SHARE,
        MENU_IMAGE_SHORT_GAME,
        MENU_IMAGE_LONG_GAME,
};

#define N_BUTTONS 3
#define N_VERTICES (N_BUTTONS * 4)

#define N_IMAGES 4

/* Size in mm of a button */
#define BUTTON_SIZE 15

#define SHORT_GAME_N_TILES 50
#define LONG_GAME_N_TILES 122
/* If the number of tiles is at least this then we’ll assume it’s a
 * long game.
 */
#define LONG_GAME_THRESHOLD ((SHORT_GAME_N_TILES + LONG_GAME_N_TILES) / 2)

static bool
menu_visible(struct vsx_menu_painter *painter)
{
        return vsx_game_state_get_dialog(painter->game_state)
                == VSX_DIALOG_MENU;
}

static bool
is_long_game(struct vsx_menu_painter *painter)
{
        return (vsx_game_state_get_n_tiles(painter->game_state) >=
                LONG_GAME_THRESHOLD);
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
        case VSX_GAME_STATE_MODIFIED_TYPE_DIALOG:
                vsx_signal_emit(&painter->redraw_needed_signal, NULL);
                break;
        case VSX_GAME_STATE_MODIFIED_TYPE_N_TILES:
                painter->vertices_dirty = true;

                if (menu_visible(painter))
                        vsx_signal_emit(&painter->redraw_needed_signal, NULL);

                break;
        default:
                break;
        }
}

static void
ensure_layout(struct vsx_menu_painter *painter)
{
        if (!painter->layout_dirty)
                return;

        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        vsx_paint_state_ensure_layout(paint_state);

        /* Convert the button size from mm to pixels */
        painter->button_size = BUTTON_SIZE * paint_state->dpi * 10 / 254;

        if (paint_state->board_rotated) {
                if (painter->button_size * N_BUTTONS > paint_state->height)
                        painter->button_size = paint_state->height / N_BUTTONS;

                painter->matrix[0] = 0.0f;
                painter->matrix[1] = (-painter->button_size * 2.0f /
                                      paint_state->height);
                painter->matrix[2] = (-painter->button_size * 2.0f /
                                      paint_state->width);
                painter->matrix[3] = 0.0f;

                painter->translation[0] =
                        painter->button_size / (float) paint_state->width;
                painter->translation[1] =
                        N_BUTTONS *
                        painter->button_size /
                        (float) paint_state->height;
        } else {
                if (painter->button_size * N_BUTTONS > paint_state->width)
                        painter->button_size = paint_state->width / N_BUTTONS;

                painter->matrix[0] = (painter->button_size * 2.0f /
                                      paint_state->width);
                painter->matrix[1] = 0.0f;
                painter->matrix[2] = 0.0f;
                painter->matrix[3] = (-painter->button_size * 2.0f /
                                      paint_state->height);

                painter->translation[0] =
                        -N_BUTTONS *
                        painter->button_size /
                        (float) paint_state->width;
                painter->translation[1] =
                        painter->button_size / (float) paint_state->height;
        }

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

        vsx_gl.glGenTextures(1, &painter->tex);

        vsx_gl.glBindTexture(GL_TEXTURE_2D, painter->tex);
        vsx_gl.glTexParameteri(GL_TEXTURE_2D,
                               GL_TEXTURE_WRAP_S,
                               GL_CLAMP_TO_EDGE);
        vsx_gl.glTexParameteri(GL_TEXTURE_2D,
                               GL_TEXTURE_WRAP_T,
                               GL_CLAMP_TO_EDGE);
        vsx_gl.glTexParameteri(GL_TEXTURE_2D,
                               GL_TEXTURE_MIN_FILTER,
                               GL_LINEAR_MIPMAP_NEAREST);
        vsx_gl.glTexParameteri(GL_TEXTURE_2D,
                               GL_TEXTURE_MAG_FILTER,
                               GL_LINEAR);

        vsx_mipmap_load_image(image, painter->tex);

        if (menu_visible(painter))
            vsx_signal_emit(&painter->redraw_needed_signal, NULL);
}

static void
create_buffer(struct vsx_menu_painter *painter)
{
        vsx_gl.glGenBuffers(1, &painter->vbo);
        vsx_gl.glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);
        vsx_gl.glBufferData(GL_ARRAY_BUFFER,
                            N_VERTICES * sizeof (struct vertex),
                            NULL, /* data */
                            GL_DYNAMIC_DRAW);

        painter->vao = vsx_array_object_new();

        vsx_array_object_set_attribute(painter->vao,
                                       VSX_SHADER_DATA_ATTRIB_POSITION,
                                       2, /* size */
                                       GL_SHORT,
                                       false, /* normalized */
                                       sizeof (struct vertex),
                                       0, /* divisor */
                                       painter->vbo,
                                       offsetof(struct vertex, x));
        vsx_array_object_set_attribute(painter->vao,
                                       VSX_SHADER_DATA_ATTRIB_TEX_COORD,
                                       2, /* size */
                                       GL_FLOAT,
                                       true, /* normalized */
                                       sizeof (struct vertex),
                                       0, /* divisor */
                                       painter->vbo,
                                       offsetof(struct vertex, s));

        painter->element_buffer =
                vsx_quad_buffer_generate(painter->vao, N_BUTTONS);
}

static void
init_program(struct vsx_menu_painter *painter,
             struct vsx_shader_data *shader_data)
{
        painter->program =
                shader_data->programs[VSX_SHADER_DATA_PROGRAM_TEXTURE];

        GLuint tex_uniform =
                vsx_gl.glGetUniformLocation(painter->program, "tex");
        vsx_gl.glUseProgram(painter->program);
        vsx_gl.glUniform1i(tex_uniform, 0);

        painter->matrix_uniform =
                vsx_gl.glGetUniformLocation(painter->program,
                                            "transform_matrix");
        painter->translation_uniform =
                vsx_gl.glGetUniformLocation(painter->program,
                                            "translation");
}

static void *
create_cb(struct vsx_game_state *game_state,
          struct vsx_painter_toolbox *toolbox)
{
        struct vsx_menu_painter *painter = vsx_calloc(sizeof *painter);

        painter->layout_dirty = true;
        painter->vertices_dirty = true;
        painter->game_state = game_state;
        painter->toolbox = toolbox;

        vsx_signal_init(&painter->redraw_needed_signal);

        init_program(painter, &toolbox->shader_data);

        create_buffer(painter);

        painter->modified_listener.notify = modified_cb;
        vsx_signal_add(vsx_game_state_get_modified_signal(game_state),
                       &painter->modified_listener);

        return painter;
}

static void
fb_size_changed_cb(void *painter_data)
{
        struct vsx_menu_painter *painter = painter_data;

        painter->layout_dirty = true;
}

static void
handle_toggle_length(struct vsx_menu_painter *painter)
{
        int n_tiles = (is_long_game(painter) ?
                       SHORT_GAME_N_TILES :
                       LONG_GAME_N_TILES);
        vsx_game_state_set_n_tiles(painter->game_state, n_tiles);
}

static bool
handle_click(struct vsx_menu_painter *painter,
             const struct vsx_input_event *event)
{
        if (!menu_visible(painter))
                return false;

        ensure_layout(painter);

        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        int x, y;

        if (paint_state->board_rotated) {
                int top_x = (paint_state->height / 2 -
                             N_BUTTONS * painter->button_size / 2);
                int top_y = (paint_state->width / 2 +
                             painter->button_size / 2);
                x = event->click.y - top_x;
                y = top_y - event->click.x;
        } else {
                int top_x = (paint_state->width / 2 -
                             N_BUTTONS * painter->button_size / 2);
                int top_y = (paint_state->height / 2 -
                             painter->button_size / 2);
                x = event->click.x - top_x;
                y = event->click.y - top_y;
        }

        if (x < 0 || x >= painter->button_size * N_BUTTONS ||
            y < 0 || y >= painter->button_size) {
                vsx_game_state_set_dialog(painter->game_state,
                                          VSX_DIALOG_NONE);
                return true;
        }

        switch (x / painter->button_size) {
        case MENU_BUTTON_LANGUAGE:
                break;
        case MENU_BUTTON_SHARE:
                vsx_game_state_set_dialog(painter->game_state,
                                          VSX_DIALOG_INVITE_LINK);
                break;
        case MENU_BUTTON_LENGTH:
                handle_toggle_length(painter);
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

        struct vertex *vertices =
                vsx_map_buffer_map(GL_ARRAY_BUFFER,
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
                }

                /* Button image */
                store_quad(v,
                           i, 0, 1, 1,
                           image / (float) N_IMAGES,
                           0.0f,
                           (image + 1.0f) / N_IMAGES,
                           1.0f);
                v += 4;
        }

        vsx_map_buffer_unmap();

        assert(v - vertices == N_VERTICES);

        painter->vertices_dirty = false;
}

static void
start_image_load(struct vsx_menu_painter *painter)
{
        if (painter->tex || painter->image_token)
                return;

        struct vsx_image_loader *image_loader =
                painter->toolbox->image_loader;

        painter->image_token = vsx_image_loader_load(image_loader,
                                                     "menu.mpng",
                                                     texture_load_cb,
                                                     painter);
}

static void
paint_cb(void *painter_data)
{
        struct vsx_menu_painter *painter = painter_data;

        if (!menu_visible(painter))
                return;

        if (painter->tex == 0) {
                start_image_load(painter);
                return;
        }

        ensure_layout(painter);

        vsx_gl.glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);

        ensure_vertices(painter);

        vsx_gl.glUseProgram(painter->program);

        vsx_gl.glUniformMatrix2fv(painter->matrix_uniform,
                                  1, /* count */
                                  GL_FALSE, /* transpose */
                                  painter->matrix);
        vsx_gl.glUniform2f(painter->translation_uniform,
                           painter->translation[0],
                           painter->translation[1]);

        vsx_array_object_bind(painter->vao);

        vsx_gl.glBindTexture(GL_TEXTURE_2D, painter->tex);

        vsx_gl_draw_range_elements(GL_TRIANGLES,
                                   0, N_VERTICES - 1,
                                   N_BUTTONS * 6,
                                   GL_UNSIGNED_SHORT,
                                   NULL /* indices */);
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

        vsx_list_remove(&painter->modified_listener.link);

        if (painter->vao)
                vsx_array_object_free(painter->vao);
        if (painter->vbo)
                vsx_gl.glDeleteBuffers(1, &painter->vbo);
        if (painter->element_buffer)
                vsx_gl.glDeleteBuffers(1, &painter->element_buffer);

        if (painter->image_token)
                vsx_image_loader_cancel(painter->image_token);
        if (painter->tex)
                vsx_gl.glDeleteTextures(1, &painter->tex);

        vsx_free(painter);
}

const struct vsx_painter
vsx_menu_painter = {
        .create_cb = create_cb,
        .fb_size_changed_cb = fb_size_changed_cb,
        .paint_cb = paint_cb,
        .input_event_cb = input_event_cb,
        .get_redraw_needed_signal_cb = get_redraw_needed_signal_cb,
        .free_cb = free_cb,
};
