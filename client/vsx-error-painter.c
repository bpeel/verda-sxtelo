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

#include "vsx-error-painter.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "vsx-mipmap.h"
#include "vsx-gl.h"
#include "vsx-array-object.h"

struct vsx_error_painter {
        struct vsx_game_state *game_state;
        struct vsx_listener modified_listener;
        struct vsx_toolbox *toolbox;

        struct vsx_array_object *vao;
        GLuint vbo;

        bool error_visible;

        float icon_size;
        float gap;

        GLuint tex;
        struct vsx_image_loader_token *image_token;

        struct vsx_main_thread_token *delay_timeout;

        struct vsx_shadow_painter_shadow *shadow;
        struct vsx_listener shadow_painter_ready_listener;

        struct vsx_signal redraw_needed_signal;
};

struct vertex {
        int16_t x, y;
        uint8_t s, t;
};

#define N_VERTICES 4

/* Size in mm of the icon */
#define ICON_SIZE 15

/* Gap in mm from the top of the screen to the icon */
#define GAP 5

static bool
can_paint(struct vsx_error_painter *painter)
{
        return (painter->error_visible &&
                painter->tex &&
                vsx_shadow_painter_is_ready(painter->toolbox->shadow_painter));
}

static void
texture_load_cb(const struct vsx_image *image,
                struct vsx_error *error,
                void *data)
{
        struct vsx_error_painter *painter = data;

        painter->image_token = NULL;

        if (error) {
                fprintf(stderr,
                        "error loading error image: %s\n",
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

        if (can_paint(painter))
                vsx_signal_emit(&painter->redraw_needed_signal, NULL);
}

static void
remove_delay_timeout(struct vsx_error_painter *painter)
{
        if (painter->delay_timeout == NULL)
                return;

        vsx_main_thread_cancel_idle(painter->delay_timeout);
        painter->delay_timeout = NULL;
}

static void
set_visible_cb(void *user_data)
{
        struct vsx_error_painter *painter = user_data;

        painter->delay_timeout = NULL;

        painter->error_visible = true;

        if (painter->tex == 0) {
                if (painter->image_token == NULL) {
                        struct vsx_image_loader *image_loader =
                                painter->toolbox->image_loader;

                        painter->image_token =
                                vsx_image_loader_load(image_loader,
                                                      "connection-lost.mpng",
                                                      texture_load_cb,
                                                      painter);
                }
        } else if (can_paint(painter)) {
                vsx_signal_emit(&painter->redraw_needed_signal, NULL);
        }
}

static void
update_error_visible(struct vsx_error_painter *painter)
{
        enum vsx_dialog dialog =
                vsx_game_state_get_dialog(painter->game_state);

        bool visible;

        if (dialog == VSX_DIALOG_NAME) {
                /* If the name dialog is visible then we haven’t tried
                 * to connect yet so there’s no need to show the
                 * error.
                 */
                visible = false;
        } else {
                visible = !vsx_game_state_get_connected(painter->game_state);
        }

        if (visible) {
                /* Set a short delay before displaying the icon in case it’s
                 * just a short glitch.
                 */
                if (painter->delay_timeout)
                        return;

                struct vsx_main_thread *main_thread =
                        painter->toolbox->main_thread;

                painter->delay_timeout =
                        vsx_main_thread_queue_timeout(main_thread,
                                                      1 * 1000 * 1000,
                                                      set_visible_cb,
                                                      painter);
        } else {
                remove_delay_timeout(painter);

                if (painter->error_visible) {
                        bool could_paint = can_paint(painter);

                        painter->error_visible = false;

                        if (could_paint) {
                                vsx_signal_emit(&painter->redraw_needed_signal,
                                                NULL);
                        }
                }
        }
}

static void
shadow_painter_ready_cb(struct vsx_listener *listener,
                        void *user_data)
{
        struct vsx_error_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_error_painter,
                                 shadow_painter_ready_listener);

        if (can_paint(painter))
                vsx_signal_emit(&painter->redraw_needed_signal, NULL);
}

static void
modified_cb(struct vsx_listener *listener,
            void *user_data)
{
        struct vsx_error_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_error_painter,
                                 modified_listener);
        const struct vsx_game_state_modified_event *event = user_data;

        switch (event->type) {
        case VSX_GAME_STATE_MODIFIED_TYPE_CONNECTED:
        case VSX_GAME_STATE_MODIFIED_TYPE_DIALOG:
                update_error_visible(painter);
                break;

        default:
                break;
        }
}

static void
generate_vertices(struct vsx_error_painter *painter,
                  struct vertex *v)
{
        v->x = 0;
        v->y = 0;
        v->s = 0;
        v->t = 0;
        v++;

        v->x = 0;
        v->y = painter->icon_size;
        v->s = 0;
        v->t = 255;
        v++;

        v->x = painter->icon_size;
        v->y = 0;
        v->s = 255;
        v->t = 0;
        v++;

        v->x = painter->icon_size;
        v->y = painter->icon_size;
        v->s = 255;
        v->t = 255;
}

static void
create_buffer(struct vsx_error_painter *painter)
{
        struct vsx_gl *gl = painter->toolbox->gl;

        struct vertex vertices[N_VERTICES];

        generate_vertices(painter, vertices);

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
                                       0, /* divisor */
                                       painter->vbo,
                                       offsetof(struct vertex, x));
        vsx_array_object_set_attribute(painter->vao,
                                       gl,
                                       VSX_SHADER_DATA_ATTRIB_TEX_COORD,
                                       2, /* size */
                                       GL_UNSIGNED_BYTE,
                                       true, /* normalized */
                                       sizeof (struct vertex),
                                       0, /* divisor */
                                       painter->vbo,
                                       offsetof(struct vertex, s));
}

static void *
create_cb(struct vsx_game_state *game_state,
          struct vsx_toolbox *toolbox)
{
        struct vsx_error_painter *painter = vsx_calloc(sizeof *painter);

        painter->game_state = game_state;
        painter->toolbox = toolbox;

        vsx_signal_init(&painter->redraw_needed_signal);

        painter->modified_listener.notify = modified_cb;
        vsx_signal_add(vsx_game_state_get_modified_signal(game_state),
                       &painter->modified_listener);

        struct vsx_paint_state *paint_state = &toolbox->paint_state;

        painter->icon_size = ICON_SIZE * paint_state->dpi / 25.4f;
        painter->gap = GAP * paint_state->dpi / 25.4f;

        create_buffer(painter);

        struct vsx_shadow_painter *shadow_painter = toolbox->shadow_painter;

        painter->shadow =
                vsx_shadow_painter_create_shadow(shadow_painter,
                                                 painter->icon_size,
                                                 painter->icon_size);

        painter->shadow_painter_ready_listener.notify =
                shadow_painter_ready_cb;
        vsx_signal_add(vsx_shadow_painter_get_ready_signal(shadow_painter),
                       &painter->shadow_painter_ready_listener);

        update_error_visible(painter);

        return painter;
}

static void
get_translation(struct vsx_error_painter *painter,
                GLfloat *translation)
{
        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        float x = (paint_state->pixel_width / 2.0f -
                   painter->icon_size / 2.0f);
        float y = painter->gap;

        vsx_paint_state_offset_pixel_translation(paint_state,
                                                 x, y,
                                                 translation);
}

static void
paint_cb(void *painter_data)
{
        struct vsx_error_painter *painter = painter_data;

        if (!can_paint(painter))
                return;

        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        vsx_paint_state_ensure_layout(paint_state);

        GLfloat translation[2];

        get_translation(painter, translation);

        const struct vsx_shader_data *shader_data =
                &painter->toolbox->shader_data;

        vsx_shadow_painter_paint(painter->toolbox->shadow_painter,
                                 painter->shadow,
                                 shader_data,
                                 paint_state->pixel_matrix,
                                 translation);

        struct vsx_gl *gl = painter->toolbox->gl;

        const struct vsx_shader_data_program_data *program =
                shader_data->programs + VSX_SHADER_DATA_PROGRAM_TEXTURE;

        gl->glUseProgram(program->program);

        gl->glUniformMatrix2fv(program->matrix_uniform,
                               1, /* count */
                               GL_FALSE, /* transpose */
                               painter->toolbox->paint_state.pixel_matrix);
        gl->glUniform2f(program->translation_uniform,
                        translation[0],
                        translation[1]);

        vsx_array_object_bind(painter->vao, gl);

        gl->glBindTexture(GL_TEXTURE_2D, painter->tex);

        gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, N_VERTICES);
}

static struct vsx_signal *
get_redraw_needed_signal_cb(void *painter_data)
{
        struct vsx_error_painter *painter = painter_data;

        return &painter->redraw_needed_signal;
}

static void
free_cb(void *painter_data)
{
        struct vsx_error_painter *painter = painter_data;

        vsx_list_remove(&painter->shadow_painter_ready_listener.link);
        vsx_list_remove(&painter->modified_listener.link);

        remove_delay_timeout(painter);

        struct vsx_gl *gl = painter->toolbox->gl;

        if (painter->vao)
                vsx_array_object_free(painter->vao, gl);
        if (painter->vbo)
                gl->glDeleteBuffers(1, &painter->vbo);

        if (painter->image_token)
                vsx_image_loader_cancel(painter->image_token);
        if (painter->tex)
                gl->glDeleteTextures(1, &painter->tex);

        if (painter->shadow) {
                vsx_shadow_painter_free_shadow(painter->toolbox->shadow_painter,
                                               painter->shadow);
        }

        vsx_free(painter);
}

const struct vsx_painter
vsx_error_painter = {
        .create_cb = create_cb,
        .paint_cb = paint_cb,
        .get_redraw_needed_signal_cb = get_redraw_needed_signal_cb,
        .free_cb = free_cb,
};
