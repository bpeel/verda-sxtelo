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
#include <assert.h>
#include <string.h>

#include "vsx-map-buffer.h"
#include "vsx-mipmap.h"
#include "vsx-gl.h"
#include "vsx-array-object.h"

struct vsx_button_painter {
        struct vsx_game_state *game_state;
        struct vsx_listener modified_listener;
        struct vsx_toolbox *toolbox;

        struct vsx_array_object *vao;
        GLuint vbo;
        struct vsx_quad_tool_buffer *quad_buffer;

        bool layout_dirty;
        bool vertices_dirty;

        float translation[2];
        int area_x, area_y;
        int area_width, area_height;
        int button_size;

        int n_quads;

        GLuint tex;
        struct vsx_image_loader_token *image_token;
};

struct vertex {
        int16_t x, y;
        float s, t;
};

#define N_BUTTONS 3
#define N_GAPS (N_BUTTONS + 1)
#define N_BUTTON_QUADS (N_BUTTONS + N_GAPS)
#define N_BUTTON_VERTICES (N_BUTTON_QUADS * 4)

#define N_BUTTONS_IN_IMAGE 4

/* The digit images occupy the space of the 4th button image. They are
 * positioned at the bottom-left of the image.
 */

/* Width of a digit in texture coordinates */
#define DIGIT_WIDTH (13.0f / 128.0f)
/* Distance between the left of one digit image to the next in texture
 * coordinates.
 */
#define DIGIT_DISTANCE_X (36.0f / 128.0f)
/* Height of a digit in texture coordinates */
#define DIGIT_HEIGHT (17.0f / (128.0f * N_BUTTONS_IN_IMAGE))
/* Distance between the bottom of one digit image to the next in
 * texture coordinates.
 */
#define DIGIT_DISTANCE_Y (42.0f / (128.0f * N_BUTTONS_IN_IMAGE))

/* Center of the number for the remaining tiles as a fraction of the
 * button size.
 */
#define REMAINING_TILES_CENTER_X (72.0f / 128.0f)
/* Bottom of the number measured as a fraction of the button size */
#define REMAINING_TILES_BOTTOM (105.0f / 128.0f)

#define DIGITS_PER_ROW 4

#define MAX_DIGITS 3

#define TOTAL_N_QUADS (N_BUTTON_QUADS + MAX_DIGITS)
#define TOTAL_N_VERTICES (TOTAL_N_QUADS * 4)

static void
modified_cb(struct vsx_listener *listener,
            void *user_data)
{
        struct vsx_button_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_button_painter,
                                 modified_listener);
        const struct vsx_game_state_modified_event *event = user_data;
        struct vsx_shell_interface *shell = painter->toolbox->shell;

        switch (event->type) {
        case VSX_GAME_STATE_MODIFIED_TYPE_REMAINING_TILES:
        case VSX_GAME_STATE_MODIFIED_TYPE_HAS_PLAYER_NAME:
                painter->vertices_dirty = true;
                shell->queue_redraw_cb(shell);
                break;

        default:
                break;
        }
}

static void
texture_load_cb(const struct vsx_image *image,
                struct vsx_error *error,
                void *data)
{
        struct vsx_button_painter *painter = data;

        painter->image_token = NULL;

        if (error) {
                struct vsx_shell_interface *shell = painter->toolbox->shell;

                shell->log_error_cb(shell,
                                    "error loading button image: %s",
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

        painter->toolbox->shell->queue_redraw_cb(painter->toolbox->shell);
}

static void
create_buffer(struct vsx_button_painter *painter)
{
        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glGenBuffers(1, &painter->vbo);
        gl->glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);
        gl->glBufferData(GL_ARRAY_BUFFER,
                         TOTAL_N_VERTICES * sizeof (struct vertex),
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
                                       painter->vbo,
                                       offsetof(struct vertex, x));
        vsx_array_object_set_attribute(painter->vao,
                                       gl,
                                       VSX_SHADER_DATA_ATTRIB_TEX_COORD,
                                       2, /* size */
                                       GL_FLOAT,
                                       false, /* normalized */
                                       sizeof (struct vertex),
                                       painter->vbo,
                                       offsetof(struct vertex, s));

        painter->quad_buffer =
                vsx_quad_tool_get_buffer(painter->toolbox->quad_tool,
                                         painter->vao,
                                         TOTAL_N_QUADS);
}

static void *
create_cb(struct vsx_game_state *game_state,
          struct vsx_toolbox *toolbox)
{
        struct vsx_button_painter *painter = vsx_calloc(sizeof *painter);

        painter->game_state = game_state;
        painter->toolbox = toolbox;

        painter->vertices_dirty = true;
        painter->layout_dirty = true;

        create_buffer(painter);

        painter->modified_listener.notify = modified_cb;
        vsx_signal_add(vsx_game_state_get_modified_signal(game_state),
                       &painter->modified_listener);

        painter->image_token = vsx_image_loader_load(toolbox->image_loader,
                                                     "buttons.mpng",
                                                     texture_load_cb,
                                                     painter);

        return painter;
}

static void
ensure_layout(struct vsx_button_painter *painter)
{
        if (!painter->layout_dirty)
                return;

        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        vsx_paint_state_ensure_layout(paint_state);

        if (paint_state->board_rotated) {
                painter->area_width =
                        (paint_state->height -
                         paint_state->board_scissor_height);
                painter->area_height = paint_state->width;
        } else {
                painter->area_width =
                        (paint_state->width -
                         paint_state->board_scissor_width);
                painter->area_height = paint_state->height;
        }

        painter->area_x = paint_state->pixel_width - painter->area_width;
        painter->area_y = 0;

        painter->button_size = MIN(painter->area_width,
                                   painter->area_height / N_BUTTONS);

        vsx_paint_state_offset_pixel_translation(paint_state,
                                                 painter->area_x,
                                                 painter->area_y,
                                                 painter->translation);

        painter->layout_dirty = false;
        painter->vertices_dirty = true;
}

static bool
handle_click(struct vsx_button_painter *painter,
             const struct vsx_input_event *event)
{
        if (!vsx_game_state_get_has_player_name(painter->game_state))
                return false;

        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        ensure_layout(painter);

        int x, y;

        vsx_paint_state_screen_to_pixel(paint_state,
                                        event->click.x,
                                        event->click.y,
                                        &x, &y);

        x -= painter->area_x;
        y -= painter->area_y;

        if (x < 0 || y < 0)
                return false;

        if (x >= painter->area_width ||
            y >= painter->area_height)
                return false;

        switch (y * N_BUTTONS / painter->area_height) {
        case 0:
                vsx_game_state_turn(painter->game_state);
                break;
        case 1:
                vsx_game_state_set_dialog(painter->game_state,
                                          VSX_DIALOG_MENU);
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
fb_size_changed_cb(void *painter_data)
{
        struct vsx_button_painter *painter = painter_data;

        painter->layout_dirty = true;
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
generate_button_vertices(struct vsx_button_painter *painter,
                         struct vertex *vertices)
{
        struct vertex *v = vertices;
        int y = 0;
        int button_size = painter->button_size;

        if (button_size <= 0) {
                /* This shouldn’t happen */
                memset(vertices, 0, TOTAL_N_VERTICES * sizeof *vertices);
                return;
        }

        int area_width = painter->area_width;
        int area_height = painter->area_height;

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

        assert(v - vertices == N_BUTTON_VERTICES);
}

static int
get_n_digits(int num)
{
        int n_digits;

        for (n_digits = 1; n_digits < MAX_DIGITS; n_digits++) {
                if (num < 10)
                        break;
                num /= 10;
        }

        return n_digits;
}

static int
generate_n_tiles_vertices(struct vsx_button_painter *painter,
                          struct vertex *vertices)
{
        int n_tiles = vsx_game_state_get_remaining_tiles(painter->game_state);
        int n_digits = get_n_digits(n_tiles);

        int button_size = painter->button_size;

        if (painter->button_size <= 0) {
                /* This shouldn’t happen */
                return 0;
        }

        int area_width = painter->area_width;
        int area_height = painter->area_height;

        int button_x = area_width / 2 - button_size / 2;
        int button_y = area_height / N_BUTTONS / 2 - button_size / 2;
        float num_left = (button_x +
                          REMAINING_TILES_CENTER_X * button_size -
                          n_digits * DIGIT_WIDTH * button_size / 2.0f);
        float num_bottom = button_y + REMAINING_TILES_BOTTOM * button_size;
        /* Digit height in pixels */
        float digit_height = DIGIT_HEIGHT * button_size * N_BUTTONS_IN_IMAGE;

        for (int i = 0; i < n_digits; i++) {
                int digit = n_tiles % 10;

                float tx = digit % DIGITS_PER_ROW * DIGIT_DISTANCE_X;
                float ty = 1.0f - digit / DIGITS_PER_ROW * DIGIT_DISTANCE_Y;

                store_quad(vertices + i * 4,
                           num_left +
                           (n_digits - i - 1) *
                           DIGIT_WIDTH * button_size,
                           num_bottom - digit_height,
                           DIGIT_WIDTH * button_size,
                           digit_height,
                           tx,
                           ty - DIGIT_HEIGHT,
                           tx + DIGIT_WIDTH,
                           ty);

                n_tiles /= 10;
        }

        memset(vertices + n_digits * 4,
               0,
               (MAX_DIGITS - n_digits) * 4 * sizeof (struct vertex));

        return n_digits;
}

static void
ensure_vertices(struct vsx_button_painter *painter)
{
        if (!painter->vertices_dirty)
                return;

        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);

        struct vertex *vertices =
                vsx_map_buffer_map(painter->toolbox->map_buffer,
                                   GL_ARRAY_BUFFER,
                                   TOTAL_N_VERTICES * sizeof (struct vertex),
                                   false, /* flush explicit */
                                   GL_DYNAMIC_DRAW);

        if (vsx_game_state_get_has_player_name(painter->game_state)) {
                generate_button_vertices(painter, vertices);
                int n_quads_for_n_tiles =
                        generate_n_tiles_vertices(painter,
                                                  vertices + N_BUTTON_VERTICES);
                painter->n_quads = n_quads_for_n_tiles + N_BUTTON_QUADS;
        } else {
                /* Draw an empty grey square instead of the buttons */
                store_quad(vertices,
                           0, 0,
                           painter->area_width,
                           painter->area_height,
                           0.0f, 0.0f, 0.0f, 0.0f);
                painter->n_quads = 1;
        }

        vsx_map_buffer_unmap(painter->toolbox->map_buffer);

        painter->vertices_dirty = false;
}

static void
paint_cb(void *painter_data)
{
        struct vsx_button_painter *painter = painter_data;

        if (painter->tex == 0)
                return;

        ensure_layout(painter);

        ensure_vertices(painter);

        const struct vsx_shader_data *shader_data =
                &painter->toolbox->shader_data;
        const struct vsx_shader_data_program_data *program =
                shader_data->programs + VSX_SHADER_DATA_PROGRAM_TEXTURE;

        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glUseProgram(program->program);
        vsx_array_object_bind(painter->vao, gl);

        gl->glUniformMatrix2fv(program->matrix_uniform,
                               1, /* count */
                               GL_FALSE, /* transpose */
                               painter->toolbox->paint_state.pixel_matrix);
        gl->glUniform2f(program->translation_uniform,
                        painter->translation[0],
                        painter->translation[1]);

        gl->glBindTexture(GL_TEXTURE_2D, painter->tex);

        vsx_gl_draw_range_elements(gl,
                                   GL_TRIANGLES,
                                   0, painter->n_quads * 4 - 1,
                                   painter->n_quads * 6,
                                   painter->quad_buffer->type,
                                   NULL /* indices */);
}

static void
free_cb(void *painter_data)
{
        struct vsx_button_painter *painter = painter_data;

        vsx_list_remove(&painter->modified_listener.link);

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
vsx_button_painter = {
        .create_cb = create_cb,
        .fb_size_changed_cb = fb_size_changed_cb,
        .paint_cb = paint_cb,
        .input_event_cb = input_event_cb,
        .free_cb = free_cb,
};
