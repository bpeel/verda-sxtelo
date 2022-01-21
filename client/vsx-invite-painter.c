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

#include "vsx-invite-painter.h"

#include <stdbool.h>
#include <string.h>

#include "vsx-map-buffer.h"
#include "vsx-gl.h"
#include "vsx-array-object.h"
#include "vsx-quad-buffer.h"
#include "vsx-qr.h"
#include "vsx-id-url.h"

struct vsx_invite_painter {
        struct vsx_game_state *game_state;
        struct vsx_listener modified_listener;

        struct vsx_painter_toolbox *toolbox;

        struct vsx_array_object *vao;
        GLuint vbo;
        GLuint element_buffer;

        bool layout_dirty;

        GLuint tex;

        /* The ID that we last used to generate the texture */
        uint64_t id_in_texture;

        struct vsx_signal redraw_needed_signal;
};

struct vertex {
        int16_t x, y;
        uint8_t s, t;
};

#define N_QUADS 1
#define N_VERTICES (N_QUADS * 4)

/* Size of the QR image in mm */
#define QR_CODE_SIZE 30

static void
modified_cb(struct vsx_listener *listener,
            void *user_data)
{
        struct vsx_invite_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_invite_painter,
                                 modified_listener);
        const struct vsx_game_state_modified_event *event = user_data;

        switch (event->type) {
        case VSX_GAME_STATE_MODIFIED_TYPE_CONVERSATION_ID:
                vsx_signal_emit(&painter->redraw_needed_signal, NULL);
                break;
        default:
                break;
        }
}

static void
free_texture(struct vsx_invite_painter *painter)
{
        if (painter->tex) {
                vsx_gl.glDeleteTextures(1, &painter->tex);
                painter->tex = 0;
        }
}

static void
create_texture(struct vsx_invite_painter *painter,
               uint64_t id)
{
        free_texture(painter);

        char url[VSX_ID_URL_ENCODED_SIZE + 1];
        vsx_id_url_encode(id, url);

        uint8_t image[VSX_QR_IMAGE_SIZE * VSX_QR_IMAGE_SIZE];
        _Static_assert(VSX_QR_DATA_SIZE == VSX_ID_URL_ENCODED_SIZE,
                       "The QR code data size must be the same size as the "
                       "invite URL");
        vsx_qr_create((const uint8_t *) url, image);

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
                               GL_NEAREST);
        vsx_gl.glTexParameteri(GL_TEXTURE_2D,
                               GL_TEXTURE_MAG_FILTER,
                               GL_NEAREST);

        vsx_gl.glTexImage2D(GL_TEXTURE_2D,
                            0, /* level */
                            GL_LUMINANCE,
                            VSX_QR_IMAGE_SIZE,
                            VSX_QR_IMAGE_SIZE,
                            0, /* border */
                            GL_LUMINANCE,
                            GL_UNSIGNED_BYTE,
                            NULL /* data */);

        for (int y = 0; y < VSX_QR_IMAGE_SIZE; y++) {
                vsx_gl.glTexSubImage2D(GL_TEXTURE_2D,
                                       0, /* level */
                                       0, /* x_offset */
                                       y,
                                       VSX_QR_IMAGE_SIZE,
                                       1, /* height */
                                       GL_LUMINANCE,
                                       GL_UNSIGNED_BYTE,
                                       image + y * VSX_QR_IMAGE_SIZE);
        }
}

static void
generate_vertices(const struct vsx_paint_state *paint_state,
                  struct vertex *vertices)
{
        /* Convert the measurements from mm to pixels */
        int qr_code_size = QR_CODE_SIZE * paint_state->dpi * 10 / 254;
        int x1 = paint_state->pixel_width / 2 - qr_code_size / 2;
        int y1 = paint_state->pixel_height / 2 - qr_code_size / 2;
        int x2 = x1 + qr_code_size;
        int y2 = y1 + qr_code_size;

        struct vertex *v = vertices;

        v->x = x1;
        v->y = y1;
        v->s = 0;
        v->t = 0;
        v++;

        v->x = x1;
        v->y = y2;
        v->s = 0;
        v->t = 255;
        v++;

        v->x = x2;
        v->y = y1;
        v->s = 255;
        v->t = 0;
        v++;

        v->x = x2;
        v->y = y2;
        v->s = 255;
        v->t = 255;
        v++;
}

static void
create_buffer(struct vsx_invite_painter *painter)
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
                                       GL_UNSIGNED_BYTE,
                                       true, /* normalized */
                                       sizeof (struct vertex),
                                       0, /* divisor */
                                       painter->vbo,
                                       offsetof(struct vertex, s));

        painter->element_buffer =
                vsx_quad_buffer_generate(painter->vao, N_QUADS);
}

static void *
create_cb(struct vsx_game_state *game_state,
          struct vsx_painter_toolbox *toolbox)
{
        struct vsx_invite_painter *painter = vsx_calloc(sizeof *painter);

        vsx_signal_init(&painter->redraw_needed_signal);

        painter->game_state = game_state;
        painter->toolbox = toolbox;

        painter->layout_dirty = true;

        create_buffer(painter);

        painter->modified_listener.notify = modified_cb;
        vsx_signal_add(vsx_game_state_get_modified_signal(game_state),
                       &painter->modified_listener);

        return painter;
}

static void
fb_size_changed_cb(void *painter_data)
{
        struct vsx_invite_painter *painter = painter_data;

        painter->layout_dirty = true;
}

static void
prepare_cb(void *painter_data)
{
        struct vsx_invite_painter *painter = painter_data;

        if (!painter->layout_dirty)
                return;

        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        vsx_paint_state_ensure_layout(paint_state);

        vsx_gl.glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);

        struct vertex *vertices =
                vsx_map_buffer_map(GL_ARRAY_BUFFER,
                                   N_VERTICES * sizeof (struct vertex),
                                   false, /* flush explicit */
                                   GL_DYNAMIC_DRAW);

        generate_vertices(paint_state, vertices);

        vsx_map_buffer_unmap();

        painter->layout_dirty = false;
}

static void
set_uniforms(struct vsx_invite_painter *painter,
             const struct vsx_shader_data_program_data *program)
{
        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        vsx_gl.glUniformMatrix2fv(program->matrix_uniform,
                                  1, /* count */
                                  GL_FALSE, /* transpose */
                                  paint_state->pixel_matrix);
        vsx_gl.glUniform2f(program->translation_uniform,
                           paint_state->pixel_translation[0],
                           paint_state->pixel_translation[1]);

}

static void
paint_cb(void *painter_data)
{
        struct vsx_invite_painter *painter = painter_data;

        uint64_t conversation_id;

        if (vsx_game_state_get_conversation_id(painter->game_state,
                                               &conversation_id)) {
                if (painter->tex == 0 ||
                    painter->id_in_texture != conversation_id) {
                        create_texture(painter, conversation_id);
                        painter->id_in_texture = conversation_id;
                }
        } else {
                return;
        }

        const struct vsx_shader_data *shader_data =
                &painter->toolbox->shader_data;
        const struct vsx_shader_data_program_data *program =
                shader_data->programs + VSX_SHADER_DATA_PROGRAM_TEXTURE;

        vsx_gl.glUseProgram(program->program);

        set_uniforms(painter, program);

        vsx_array_object_bind(painter->vao);

        vsx_gl.glBindTexture(GL_TEXTURE_2D, painter->tex);

        vsx_gl_draw_range_elements(GL_TRIANGLES,
                                   0, N_VERTICES - 1,
                                   N_QUADS * 6,
                                   GL_UNSIGNED_SHORT,
                                   NULL /* indices */);
}

static bool
input_event_cb(void *painter_data,
               const struct vsx_input_event *event)
{
        struct vsx_invite_painter *painter = painter_data;

        switch (event->type) {
        case VSX_INPUT_EVENT_TYPE_DRAG_START:
        case VSX_INPUT_EVENT_TYPE_DRAG:
        case VSX_INPUT_EVENT_TYPE_ZOOM_START:
        case VSX_INPUT_EVENT_TYPE_ZOOM:
                return false;

        case VSX_INPUT_EVENT_TYPE_CLICK:
                vsx_game_state_set_dialog(painter->game_state,
                                          VSX_DIALOG_NONE);
                return true;
        }

        return false;
}

static struct vsx_signal *
get_redraw_needed_signal_cb(void *painter_data)
{
        struct vsx_invite_painter *painter = painter_data;

        return &painter->redraw_needed_signal;
}

static void
free_cb(void *painter_data)
{
        struct vsx_invite_painter *painter = painter_data;

        vsx_list_remove(&painter->modified_listener.link);

        if (painter->vao)
                vsx_array_object_free(painter->vao);
        if (painter->vbo)
                vsx_gl.glDeleteBuffers(1, &painter->vbo);
        if (painter->element_buffer)
                vsx_gl.glDeleteBuffers(1, &painter->element_buffer);

        free_texture(painter);

        vsx_free(painter);
}

const struct vsx_painter
vsx_invite_painter = {
        .create_cb = create_cb,
        .fb_size_changed_cb = fb_size_changed_cb,
        .prepare_cb = prepare_cb,
        .paint_cb = paint_cb,
        .input_event_cb = input_event_cb,
        .get_redraw_needed_signal_cb = get_redraw_needed_signal_cb,
        .free_cb = free_cb,
};
