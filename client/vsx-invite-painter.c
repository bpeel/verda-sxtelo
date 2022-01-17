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

#define TEXTURE_SIZE 64

_Static_assert(VSX_QR_IMAGE_SIZE <= TEXTURE_SIZE,
               "The texture needs to be big enough to hold the QR image");

struct vsx_invite_painter {
        struct vsx_game_state *game_state;
        struct vsx_listener modified_listener;

        struct vsx_painter_toolbox *toolbox;

        GLuint program;
        GLint matrix_uniform;
        GLint translation_uniform;

        struct vsx_array_object *vao;
        GLuint vbo;
        GLuint element_buffer;

        GLuint tex;

        /* The ID that we last used to generate the texture */
        uint64_t id_in_texture;

        struct vsx_signal redraw_needed_signal;
};

struct vertex {
        int16_t x, y;
        uint16_t s, t;
};

#define N_QUADS 1
#define N_VERTICES (N_QUADS * 4)

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
                            TEXTURE_SIZE,
                            TEXTURE_SIZE,
                            0, /* border */
                            GL_LUMINANCE,
                            GL_UNSIGNED_BYTE,
                            NULL /* data */);

        uint8_t row[TEXTURE_SIZE];

        memset(row + VSX_QR_IMAGE_SIZE,
               0xff,
               TEXTURE_SIZE - VSX_QR_IMAGE_SIZE);

        for (int y = 0; y < VSX_QR_IMAGE_SIZE; y++) {
                memcpy(row, image + y * VSX_QR_IMAGE_SIZE, VSX_QR_IMAGE_SIZE);
                vsx_gl.glTexSubImage2D(GL_TEXTURE_2D,
                                       0, /* level */
                                       0, /* x_offset */
                                       y,
                                       TEXTURE_SIZE,
                                       1, /* height */
                                     GL_LUMINANCE,
                                       GL_UNSIGNED_BYTE,
                                       row);
        }

        memset(row, 0xff, VSX_QR_IMAGE_SIZE);

        for (int y = VSX_QR_IMAGE_SIZE; y < TEXTURE_SIZE; y++) {
                vsx_gl.glTexSubImage2D(GL_TEXTURE_2D,
                                       0, /* level */
                                       0, /* x_offset */
                                       y,
                                       TEXTURE_SIZE,
                                       1, /* height */
                                       GL_LUMINANCE,
                                       GL_UNSIGNED_BYTE,
                                       row);
        }
}

static void
generate_vertices(struct vertex *vertices)
{
        struct vertex *v = vertices;

        uint16_t tex_max = VSX_QR_IMAGE_SIZE * 65535 / TEXTURE_SIZE;

        v->x = -1;
        v->y = 1;
        v->s = 0;
        v->t = 0;
        v++;

        v->x = -1;
        v->y = -1;
        v->s = 0;
        v->t = tex_max;
        v++;

        v->x = 1;
        v->y = 1;
        v->s = tex_max;
        v->t = 0;
        v++;

        v->x = 1;
        v->y = -1;
        v->s = tex_max;
        v->t = tex_max;
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
                            GL_STATIC_DRAW);

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
                                       GL_UNSIGNED_SHORT,
                                       true, /* normalized */
                                       sizeof (struct vertex),
                                       0, /* divisor */
                                       painter->vbo,
                                       offsetof(struct vertex, s));

        struct vertex *vertices =
                vsx_map_buffer_map(GL_ARRAY_BUFFER,
                                   N_VERTICES * sizeof (struct vertex),
                                   false, /* flush explicit */
                                   GL_STATIC_DRAW);

        generate_vertices(vertices);

        vsx_map_buffer_unmap();

        painter->element_buffer =
                vsx_quad_buffer_generate(painter->vao, N_QUADS);
}

static void
init_program(struct vsx_invite_painter *painter,
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
        struct vsx_invite_painter *painter = vsx_calloc(sizeof *painter);

        vsx_signal_init(&painter->redraw_needed_signal);

        painter->game_state = game_state;
        painter->toolbox = toolbox;

        init_program(painter, &toolbox->shader_data);

        create_buffer(painter);

        painter->modified_listener.notify = modified_cb;
        vsx_signal_add(vsx_game_state_get_modified_signal(game_state),
                       &painter->modified_listener);

        return painter;
}

static void
set_uniforms(struct vsx_invite_painter *painter)
{
        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        vsx_paint_state_ensure_layout(paint_state);

        GLfloat matrix[4];

        if (paint_state->board_rotated) {
                matrix[0] = 0.0f;
                matrix[1] = -0.5f * paint_state->width / paint_state->height;
                matrix[2] = 0.5f;
                matrix[3] = 0.0f;
        } else {
                matrix[0] = 0.5f * paint_state->height / paint_state->width;
                matrix[1] = 0.0f;
                matrix[2] = 0.0f;
                matrix[3] = 0.5f;
        }

        vsx_gl.glUniformMatrix2fv(painter->matrix_uniform,
                                  1, /* count */
                                  GL_FALSE, /* transpose */
                                  matrix);
        vsx_gl.glUniform2f(painter->translation_uniform, 0.0f, 0.0f);

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

        vsx_gl.glUseProgram(painter->program);

        set_uniforms(painter);

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
        .paint_cb = paint_cb,
        .input_event_cb = input_event_cb,
        .get_redraw_needed_signal_cb = get_redraw_needed_signal_cb,
        .free_cb = free_cb,
};
