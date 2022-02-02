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

#include "vsx-fireworks-painter.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "vsx-mipmap.h"
#include "vsx-gl.h"
#include "vsx-array-object.h"
#include "vsx-board.h"
#include "vsx-monotonic.h"

struct player_buffer {
        struct vsx_array_object *vao;
        GLuint vbo;
};

struct vsx_fireworks_painter {
        struct vsx_game_state *game_state;
        struct vsx_listener event_listener;
        struct vsx_toolbox *toolbox;

        /* True if the GL implementation can support the effect, ie,
         * if it can paint the required point size. Otherwise we’ll
         * just skip the effect.
         */
        bool supports_effect;

        struct player_buffer player_buffers[VSX_BOARD_N_PLAYER_SPACES];

        GLuint tex;
        struct vsx_image_loader_token *image_token;

        int64_t fireworks_start_time;
        int shouting_player;

        GLint elapsed_time_uniform;
        GLint start_point_uniform;

        struct vsx_signal redraw_needed_signal;
};

struct vertex {
        int16_t x, y;
        uint8_t color[3];
};

#define N_VERTICES 128

/* Point size in mm */
#define POINT_SIZE 2

/* Duration of the effect in microsecords */
#define FIREWORKS_DURATION (1000 * 1000)

struct fire_properties {
        /* The angle to fire the fireworks at, measured in clockwise
         * radians where 0 is straight up.
         */
        float angle;
        /* The range of random velocities */
        float min_velocity, max_velocity;
};

static const struct fire_properties
fire_properties[] = {
        {
                /* Straight up */
                .angle = 0.0f,
                .min_velocity = 150.0f, .max_velocity = 300.0f,
        },
        {
                /* Straight up */
                .angle = 0.0f,
                .min_velocity = 150.0f, .max_velocity = 600.0f,
        },
        {
                /* To the right */
                .angle = M_PI * 0.5f,
                .min_velocity = 150.0f, .max_velocity = 600.0f,
        },
        {
                /* To the left */
                .angle = M_PI * 1.5f,
                .min_velocity = 150.0f, .max_velocity = 600.0f,
        },
        {
                /* Right and upwards */
                .angle = M_PI * 0.25f,
                .min_velocity = 150.0f, .max_velocity = 600.0f,
        },
        {
                /* Left and upwards */
                .angle = M_PI * 1.75f,
                .min_velocity = 150.0f, .max_velocity = 600.0f,
        },
};

_Static_assert(VSX_N_ELEMENTS(fire_properties) == VSX_BOARD_N_PLAYER_SPACES,
               "There should be exactly one fire angle for each player space.");

/* Returns the elapsed time of the effect, or -1 if no effect should
 * be drawn
 */
static int
get_elapsed_time(struct vsx_fireworks_painter *painter)
{
        if (painter->fireworks_start_time == 0)
                return -1;

        if (!painter->supports_effect)
                return -1;

        if (painter->tex == 0)
                return -1;

        int64_t now = vsx_monotonic_get();

        int64_t elapsed_time = now - painter->fireworks_start_time;

        if (elapsed_time >= FIREWORKS_DURATION) {
                painter->fireworks_start_time = 0;
                return -1;
        }

        return elapsed_time;
}

static void
texture_load_cb(const struct vsx_image *image,
                struct vsx_error *error,
                void *data)
{
        struct vsx_fireworks_painter *painter = data;

        painter->image_token = NULL;

        if (error) {
                fprintf(stderr,
                        "error loading fireworks image: %s\n",
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

        if (get_elapsed_time(painter) != -1)
                vsx_signal_emit(&painter->redraw_needed_signal, NULL);
}

static void
start_fireworks(struct vsx_fireworks_painter *painter,
                int player_num)
{
        painter->fireworks_start_time = vsx_monotonic_get();
        painter->shouting_player = player_num;

        if (get_elapsed_time(painter) != -1)
                vsx_signal_emit(&painter->redraw_needed_signal, NULL);
}

static void
event_cb(struct vsx_listener *listener,
         void *user_data)
{
        struct vsx_fireworks_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_fireworks_painter,
                                 event_listener);
        const struct vsx_connection_event *event = user_data;

        switch (event->type) {
        case VSX_CONNECTION_EVENT_TYPE_PLAYER_SHOUTED:
                if (event->synced &&
                    event->player_shouted.player_num <
                    VSX_BOARD_N_PLAYER_SPACES) {
                        start_fireworks(painter,
                                        event->player_shouted.player_num);
                }
                break;

        default:
                break;
        }
}

static void
generate_vertices(struct vertex *v,
                  const struct fire_properties *props)
{
        for (unsigned i = 0; i < N_VERTICES; i++) {
                /* The x/y position is used as an initial velocity
                 * vector for the point, measured in board units per
                 * duration of the effect.
                 */
                float velocity = (rand() / (float) RAND_MAX *
                                  (props->max_velocity -
                                   props->min_velocity) +
                                  props->min_velocity);
                /* Angle in radians where 0 is straight up. Pick a
                 * random around that in a range of 45°.
                 */
                float angle = (props->angle +
                               (rand() & 0xff) * M_PI / 512.0 -
                               M_PI / 4.0);

                v->x = velocity * sinf(angle);
                v->y = -velocity * cosf(angle);

                int int_color = rand() % 6 + 1;

                for (int i = 0; i < 3; i++) {
                        v->color[i] = (int_color & 1) * 255;
                        int_color >>= 1;
                }

                v++;
        }
}

static void
create_buffer(struct vsx_fireworks_painter *painter,
              int player_num)
{
        struct vsx_gl *gl = painter->toolbox->gl;

        struct player_buffer *buffer = painter->player_buffers + player_num;

        gl->glGenBuffers(1, &buffer->vbo);
        gl->glBindBuffer(GL_ARRAY_BUFFER, buffer->vbo);
        gl->glBufferData(GL_ARRAY_BUFFER,
                         N_VERTICES * sizeof (struct vertex),
                         NULL, /* data */
                         GL_STATIC_DRAW);

        struct vertex *vertices =
                vsx_map_buffer_map(painter->toolbox->map_buffer,
                                   GL_ARRAY_BUFFER,
                                   N_VERTICES * sizeof (struct vertex),
                                   false, /* flush_explicit */
                                   GL_STATIC_DRAW);

        generate_vertices(vertices, fire_properties + player_num);

        vsx_map_buffer_unmap(painter->toolbox->map_buffer);

        buffer->vao = vsx_array_object_new(gl);

        vsx_array_object_set_attribute(buffer->vao,
                                       gl,
                                       VSX_SHADER_DATA_ATTRIB_POSITION,
                                       2, /* size */
                                       GL_SHORT,
                                       false, /* normalized */
                                       sizeof (struct vertex),
                                       0, /* divisor */
                                       buffer->vbo,
                                       offsetof(struct vertex, x));
        vsx_array_object_set_attribute(buffer->vao,
                                       gl,
                                       VSX_SHADER_DATA_ATTRIB_COLOR,
                                       3, /* size */
                                       GL_UNSIGNED_BYTE,
                                       true, /* normalized */
                                       sizeof (struct vertex),
                                       0, /* divisor */
                                       buffer->vbo,
                                       offsetof(struct vertex, color));
}

static void
init_uniforms(struct vsx_fireworks_painter *painter)
{
        const struct vsx_shader_data *shader_data =
                &painter->toolbox->shader_data;
        const struct vsx_shader_data_program_data *program =
                shader_data->programs + VSX_SHADER_DATA_PROGRAM_FIREWORKS;
        struct vsx_gl *gl = painter->toolbox->gl;

        GLint point_size_uniform =
                gl->glGetUniformLocation(program->program, "point_size");

        float point_size = (POINT_SIZE *
                            painter->toolbox->paint_state.dpi /
                            25.4f);

        gl->glUseProgram(program->program);
        gl->glUniform1f(point_size_uniform, point_size);

        painter->elapsed_time_uniform =
                gl->glGetUniformLocation(program->program, "elapsed_time");

        painter->start_point_uniform =
                gl->glGetUniformLocation(program->program, "start_point");

        GLfloat point_size_range[2];

        gl->glGetFloatv(GL_ALIASED_POINT_SIZE_RANGE, point_size_range);

        painter->supports_effect = (point_size >= point_size_range[0] &&
                                    point_size <= point_size_range[1]);
}

static void *
create_cb(struct vsx_game_state *game_state,
          struct vsx_toolbox *toolbox)
{
        struct vsx_fireworks_painter *painter = vsx_calloc(sizeof *painter);

        painter->game_state = game_state;
        painter->toolbox = toolbox;

        vsx_signal_init(&painter->redraw_needed_signal);

        painter->event_listener.notify = event_cb;
        vsx_signal_add(vsx_game_state_get_event_signal(game_state),
                       &painter->event_listener);

        init_uniforms(painter);

        if (painter->supports_effect) {
                for (int i = 0; i < VSX_BOARD_N_PLAYER_SPACES; i++)
                        create_buffer(painter, i);

                painter->image_token =
                        vsx_image_loader_load(painter->toolbox->image_loader,
                                              "firework.mpng",
                                              texture_load_cb,
                                              painter);
        }

        return painter;
}

static void
paint_cb(void *painter_data)
{
        struct vsx_fireworks_painter *painter = painter_data;

        int elapsed_time = get_elapsed_time(painter);

        if (elapsed_time == -1)
                return;

        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        vsx_paint_state_ensure_layout(paint_state);

        const struct vsx_shader_data *shader_data =
                &painter->toolbox->shader_data;
        const struct vsx_shader_data_program_data *program =
                shader_data->programs + VSX_SHADER_DATA_PROGRAM_FIREWORKS;

        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl->glEnable(GL_BLEND);

        gl->glBindTexture(GL_TEXTURE_2D, painter->tex);

        gl->glUseProgram(program->program);

        gl->glUniformMatrix2fv(program->matrix_uniform,
                               1, /* count */
                               GL_FALSE, /* transpose */
                               painter->toolbox->paint_state.board_matrix);
        gl->glUniform2f(program->translation_uniform,
                        painter->toolbox->paint_state.board_translation[0],
                        painter->toolbox->paint_state.board_translation[1]);

        gl->glUniform1f(painter->elapsed_time_uniform,
                        elapsed_time / (float) FIREWORKS_DURATION);

        const struct vsx_board_player_space *space =
                vsx_board_player_spaces + painter->shouting_player;

        gl->glUniform2f(painter->start_point_uniform,
                        space->center_x,
                        space->center_y);

        struct player_buffer *buffer =
                painter->player_buffers + painter->shouting_player;

        vsx_array_object_bind(buffer->vao, gl);

        gl->glEnable(GL_SCISSOR_TEST);
        gl->glScissor(paint_state->board_scissor_x,
                      paint_state->board_scissor_y,
                      paint_state->board_scissor_width,
                      paint_state->board_scissor_height);

        gl->glDrawArrays(GL_POINTS, 0, N_VERTICES);

        gl->glDisable(GL_SCISSOR_TEST);

        gl->glDisable(GL_BLEND);

        /* Queue a redraw immediately to animate the effect */
        vsx_signal_emit(&painter->redraw_needed_signal, NULL);
}

static struct vsx_signal *
get_redraw_needed_signal_cb(void *painter_data)
{
        struct vsx_fireworks_painter *painter = painter_data;

        return &painter->redraw_needed_signal;
}

static void
free_cb(void *painter_data)
{
        struct vsx_fireworks_painter *painter = painter_data;

        vsx_list_remove(&painter->event_listener.link);

        struct vsx_gl *gl = painter->toolbox->gl;

        for (int i = 0; i < VSX_BOARD_N_PLAYER_SPACES; i++) {
                struct player_buffer *buffer = painter->player_buffers + i;

                if (buffer->vao)
                        vsx_array_object_free(buffer->vao, gl);
                if (buffer->vbo)
                        gl->glDeleteBuffers(1, &buffer->vbo);
        }

        if (painter->image_token)
                vsx_image_loader_cancel(painter->image_token);
        if (painter->tex)
                gl->glDeleteTextures(1, &painter->tex);

        vsx_free(painter);
}

const struct vsx_painter
vsx_fireworks_painter = {
        .create_cb = create_cb,
        .paint_cb = paint_cb,
        .get_redraw_needed_signal_cb = get_redraw_needed_signal_cb,
        .free_cb = free_cb,
};
