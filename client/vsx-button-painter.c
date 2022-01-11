/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2021  Neil Roberts
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

#include "vsx-button-painter.h"

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

struct vsx_button_painter {
        struct vsx_game_state *game_state;
        struct vsx_painter_toolbox *toolbox;

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

#define N_BUTTONS 3
#define N_GAPS (N_BUTTONS + 1)
#define N_QUADS (N_BUTTONS + N_GAPS)
#define N_VERTICES (N_QUADS * 4)

#define N_BUTTONS_IN_IMAGE 4

static void
texture_load_cb(const struct vsx_image *image,
                struct vsx_error *error,
                void *data)
{
        struct vsx_button_painter *painter = data;

        painter->image_token = NULL;

        if (error) {
                fprintf(stderr,
                        "error loading button image: %s\n",
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

        vsx_signal_emit(&painter->redraw_needed_signal, NULL);
}

static void
create_buffer(struct vsx_button_painter *painter)
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
                vsx_quad_buffer_generate(painter->vao, N_QUADS);
}

static void
init_program(struct vsx_button_painter *painter,
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
        struct vsx_button_painter *painter = vsx_calloc(sizeof *painter);

        painter->game_state = game_state;
        painter->toolbox = toolbox;

        vsx_signal_init(&painter->redraw_needed_signal);

        init_program(painter, &toolbox->shader_data);

        create_buffer(painter);

        painter->image_token = vsx_image_loader_load(toolbox->image_loader,
                                                     "buttons.mpng",
                                                     texture_load_cb,
                                                     painter);

        return painter;
}

static bool
handle_click(struct vsx_button_painter *painter,
             const struct vsx_input_event *event)
{
        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        vsx_paint_state_ensure_layout(paint_state);

        int x, y;

        if (paint_state->board_rotated) {
                /* This takes into account the inverted y-origin of
                 * the board scissor
                 */
                x = (event->click.y -
                     (paint_state->height -
                      paint_state->board_scissor_y));
                y = paint_state->width - 1 - event->click.x;
        } else {
                x = (event->click.x -
                     paint_state->board_scissor_x -
                     paint_state->board_scissor_width);
                y = event->click.y;
        }

        if (x < 0 || y < 0)
                return false;

        if (x >= paint_state->button_area_width ||
            y >= paint_state->button_area_height)
                return false;

        switch (y * N_BUTTONS / paint_state->button_area_height) {
        case 0:
                vsx_game_state_turn(painter->game_state);
                break;
        case 2:
                vsx_game_state_shout(painter->game_state);
                break;
        }

        return false;
}

static bool
input_event_cb(void *painter_data,
               const struct vsx_input_event *event)
{
        struct vsx_button_painter *painter = painter_data;

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
generate_vertices(struct vsx_button_painter *painter,
                  struct vertex *vertices)
{
        struct vertex *v = vertices;
        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;
        int button_size = MIN(paint_state->button_area_width,
                              paint_state->button_area_height / N_BUTTONS);
        int y = 0;

        if (button_size <= 0) {
                /* This shouldn’t happen */
                memset(vertices, 0, N_VERTICES * sizeof *vertices);
                return;
        }

        int area_width = paint_state->button_area_width;
        int area_height = paint_state->button_area_height;

        for (int i = 0; i < N_BUTTONS; i++) {
                int button_start = (i * area_height / N_BUTTONS +
                                    area_height / N_BUTTONS / 2 -
                                    button_size / 2);

                /* Gap above each button */
                store_quad(v,
                           0, y,
                           area_width, button_start - y,
                           0.0f, 0.0f, 0.0f, 0.0f);
                y = button_start;
                v += 4;

                float tex_coord_side_extra =
                        (area_width - button_size) / 2.0f / button_size;

                /* Button image */
                store_quad(v,
                           0, y,
                           area_width, button_size,
                           -tex_coord_side_extra,
                           i / (float) N_BUTTONS_IN_IMAGE,
                           1.0f + tex_coord_side_extra,
                           (i + 1.0f) / N_BUTTONS_IN_IMAGE);
                y += button_size;
                v += 4;
        }

        /* Gap under all the buttons */
        store_quad(v,
                   0, y,
                   area_width, area_height - y,
                   0.0f, 0.0f, 0.0f, 0.0f);
        v += 4;

        assert(v - vertices == N_VERTICES);
}

static void
paint_cb(void *painter_data)
{
        struct vsx_button_painter *painter = painter_data;

        if (painter->tex == 0)
                return;

        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        vsx_paint_state_ensure_layout(paint_state);

        vsx_gl.glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);

        struct vertex *vertices =
                vsx_map_buffer_map(GL_ARRAY_BUFFER,
                                   N_VERTICES * sizeof (struct vertex),
                                   false, /* flush explicit */
                                   GL_DYNAMIC_DRAW);

        generate_vertices(painter, vertices);

        vsx_map_buffer_unmap();

        vsx_gl.glUseProgram(painter->program);
        vsx_array_object_bind(painter->vao);

        vsx_gl.glUniformMatrix2fv(painter->matrix_uniform,
                                  1, /* count */
                                  GL_FALSE, /* transpose */
                                  paint_state->button_area_matrix);
        vsx_gl.glUniform2f(painter->translation_uniform,
                           paint_state->button_area_translation[0],
                           paint_state->button_area_translation[1]);

        vsx_gl.glBindTexture(GL_TEXTURE_2D, painter->tex);

        vsx_gl_draw_range_elements(GL_TRIANGLES,
                                   0, N_VERTICES - 1,
                                   N_QUADS * 6,
                                   GL_UNSIGNED_SHORT,
                                   NULL /* indices */);
}

static struct vsx_signal *
get_redraw_needed_signal_cb(void *painter_data)
{
        struct vsx_button_painter *painter = painter_data;

        return &painter->redraw_needed_signal;
}

static void
free_cb(void *painter_data)
{
        struct vsx_button_painter *painter = painter_data;

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
vsx_button_painter = {
        .create_cb = create_cb,
        .paint_cb = paint_cb,
        .input_event_cb = input_event_cb,
        .get_redraw_needed_signal_cb = get_redraw_needed_signal_cb,
        .free_cb = free_cb,
};
