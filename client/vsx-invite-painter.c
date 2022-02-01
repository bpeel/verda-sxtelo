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
#include <math.h>

#include "vsx-map-buffer.h"
#include "vsx-gl.h"
#include "vsx-array-object.h"
#include "vsx-quad-buffer.h"
#include "vsx-qr.h"
#include "vsx-id-url.h"
#include "vsx-layout.h"

struct vsx_invite_painter {
        struct vsx_game_state *game_state;
        struct vsx_listener modified_listener;

        struct vsx_toolbox *toolbox;

        struct vsx_array_object *vao;
        GLuint vbo;
        GLuint element_buffer;

        bool layout_dirty;
        int dialog_x, dialog_y;
        int dialog_width, dialog_height;

        struct vsx_layout_paint_position paragraphs[2];

        GLuint tex;

        /* The ID that we last used to generate the texture */
        uint64_t id_in_texture;

        struct vsx_shadow_painter_shadow *shadow;
        struct vsx_listener shadow_painter_ready_listener;

        struct vsx_signal redraw_needed_signal;
};

struct vertex {
        int16_t x, y;
        float s, t;
};

#define N_QUADS 1
#define N_VERTICES (N_QUADS * 4)

/* Size of the QR image in mm */
#define QR_CODE_SIZE 30

/* Max width of the explanation text in mm */
#define PARAGRAPH_WIDTH 40
/* Border size around the paragraphs in mm. This is chosen to be the
 * same size as the quiet zone around the qr code.
 */
#define BORDER (4 * QR_CODE_SIZE / VSX_QR_IMAGE_SIZE)

#define PARAGRAPHS_FONT VSX_FONT_TYPE_LABEL

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
        case VSX_GAME_STATE_MODIFIED_TYPE_LANGUAGE:
                painter->layout_dirty = true;
                vsx_signal_emit(&painter->redraw_needed_signal, NULL);
                break;
        case VSX_GAME_STATE_MODIFIED_TYPE_CONVERSATION_ID:
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
        struct vsx_invite_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_invite_painter,
                                 shadow_painter_ready_listener);

        vsx_signal_emit(&painter->redraw_needed_signal, NULL);
}

static void
clear_shadow(struct vsx_invite_painter *painter)
{
        if (painter->shadow == NULL)
                return;

        vsx_shadow_painter_free_shadow(painter->toolbox->shadow_painter,
                                       painter->shadow);
        painter->shadow = NULL;
}

static void
create_shadow(struct vsx_invite_painter *painter)
{
        clear_shadow(painter);

        struct vsx_shadow_painter *shadow_painter =
                painter->toolbox->shadow_painter;

        int w = painter->dialog_width;
        int h = painter->dialog_height;

        painter->shadow =
                vsx_shadow_painter_create_shadow(shadow_painter, w, h);
}

static void
free_texture(struct vsx_invite_painter *painter)
{
        if (painter->tex) {
                struct vsx_gl *gl = painter->toolbox->gl;

                gl->glDeleteTextures(1, &painter->tex);
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
                            GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D,
                            GL_TEXTURE_MAG_FILTER,
                            GL_NEAREST);

        gl->glTexImage2D(GL_TEXTURE_2D,
                         0, /* level */
                         GL_LUMINANCE,
                         VSX_QR_IMAGE_SIZE,
                         VSX_QR_IMAGE_SIZE,
                         0, /* border */
                         GL_LUMINANCE,
                         GL_UNSIGNED_BYTE,
                         NULL /* data */);

        for (int y = 0; y < VSX_QR_IMAGE_SIZE; y++) {
                gl->glTexSubImage2D(GL_TEXTURE_2D,
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
update_vertices(struct vsx_invite_painter *painter,
                int qr_code_size,
                int total_width,
                int total_height)
{
        const struct vsx_paint_state *paint_state =
                &painter->toolbox->paint_state;

        int x1 = paint_state->pixel_width / 2 - total_width / 2;
        int y1 = paint_state->pixel_height / 2 - total_height / 2;
        int x2 = x1 + total_width;
        int y2 = y1 + total_height;

        float s1 = 0.0f;
        float s2 = total_width / (float) qr_code_size;

        float height_in_tex_coords = total_height / (float) qr_code_size;
        float t1 = -height_in_tex_coords / 2.0f + 0.5f;
        float t2 = t1 + height_in_tex_coords;

        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glBindBuffer(GL_ARRAY_BUFFER, painter->vbo);

        struct vertex *v =
                vsx_map_buffer_map(painter->toolbox->map_buffer,
                                   GL_ARRAY_BUFFER,
                                   N_VERTICES * sizeof (struct vertex),
                                   false, /* flush explicit */
                                   GL_DYNAMIC_DRAW);

        v->x = x1;
        v->y = y1;
        v->s = s1;
        v->t = t1;
        v++;

        v->x = x1;
        v->y = y2;
        v->s = s1;
        v->t = t2;
        v++;

        v->x = x2;
        v->y = y1;
        v->s = s2;
        v->t = t1;
        v++;

        v->x = x2;
        v->y = y2;
        v->s = s2;
        v->t = t2;
        v++;

        vsx_map_buffer_unmap(painter->toolbox->map_buffer);
}

static void
create_buffer(struct vsx_invite_painter *painter)
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

        painter->element_buffer =
                vsx_quad_buffer_generate(painter->vao,
                                         gl,
                                         painter->toolbox->map_buffer,
                                         N_QUADS);
}

static void
create_layouts(struct vsx_invite_painter *painter)
{
        for (int i = 0; i < VSX_N_ELEMENTS(painter->paragraphs); i++) {
                struct vsx_layout *layout =
                        vsx_layout_new(painter->toolbox);

                vsx_layout_set_font(layout, PARAGRAPHS_FONT);

                painter->paragraphs[i].layout = layout;
        }

        painter->paragraphs[1].r = 0.106f;
        painter->paragraphs[1].g = 0.561f;
        painter->paragraphs[1].b = 0.871f;
}

static void *
create_cb(struct vsx_game_state *game_state,
          struct vsx_toolbox *toolbox)
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

        painter->shadow_painter_ready_listener.notify =
                shadow_painter_ready_cb;
        struct vsx_shadow_painter *shadow_painter = toolbox->shadow_painter;
        vsx_signal_add(vsx_shadow_painter_get_ready_signal(shadow_painter),
                       &painter->shadow_painter_ready_listener);

        create_layouts(painter);

        return painter;
}

static void
fb_size_changed_cb(void *painter_data)
{
        struct vsx_invite_painter *painter = painter_data;

        painter->layout_dirty = true;
}

static void
update_layouts(struct vsx_invite_painter *painter,
               uint64_t conversation_id)
{
        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;
        int paragraph_width = PARAGRAPH_WIDTH * paint_state->dpi * 10 / 254;

        vsx_layout_set_width(painter->paragraphs[0].layout, paragraph_width);

        enum vsx_text_language language =
                vsx_game_state_get_language(painter->game_state);
        vsx_layout_set_text(painter->paragraphs[0].layout,
                            vsx_text_get(language,
                                         VSX_TEXT_INVITE_EXPLANATION));

        char id_url[VSX_ID_URL_ENCODED_SIZE + 1];
        vsx_id_url_encode(conversation_id, id_url);
        vsx_layout_set_text(painter->paragraphs[1].layout, id_url);

        for (int i = 0; i < VSX_N_ELEMENTS(painter->paragraphs); i++)
                vsx_layout_prepare(painter->paragraphs[i].layout);
}

static void
get_paragraphs_size(struct vsx_invite_painter *painter,
                    int *width_out,
                    int *height_out)
{
        struct vsx_font_library *font_library = painter->toolbox->font_library;
        struct vsx_font *font = vsx_font_library_get_font(font_library,
                                                          PARAGRAPHS_FONT);
        struct vsx_font_metrics font_metrics;

        vsx_font_get_metrics(font, &font_metrics);

        int y_advance = roundf(font_metrics.height);

        int max_right = 0;
        int first_top = 0;
        int y = 0;

        for (int i = 0; i < VSX_N_ELEMENTS(painter->paragraphs); i++) {
                struct vsx_layout_paint_position *paragraph =
                        painter->paragraphs + i;
                const struct vsx_layout_extents *extents =
                        vsx_layout_get_logical_extents(paragraph->layout);

                paragraph->x = 0;
                paragraph->y = y + extents->top;

                if (i == 0)
                        first_top = extents->top;

                if (i >= VSX_N_ELEMENTS(painter->paragraphs) - 1)
                        y += extents->bottom;
                else
                        y += (extents->n_lines + 1) * y_advance;

                if (extents->right > max_right)
                        max_right = extents->right;
        }

        *width_out = max_right;
        *height_out = first_top + y;
}

static void
ensure_layout(struct vsx_invite_painter *painter)
{
        uint64_t conversation_id;

        if (vsx_game_state_get_conversation_id(painter->game_state,
                                               &conversation_id)) {
                if (painter->tex == 0 ||
                    painter->id_in_texture != conversation_id) {
                        create_texture(painter, conversation_id);
                        painter->id_in_texture = conversation_id;
                } else if (!painter->layout_dirty) {
                        return;
                }
        } else {
                free_texture(painter);
                return;
        }

        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        vsx_paint_state_ensure_layout(paint_state);

        update_layouts(painter, conversation_id);

        /* Convert the measurements from mm to pixels */
        int border = BORDER * paint_state->dpi * 10 / 254;
        int qr_code_size = QR_CODE_SIZE * paint_state->dpi * 10 / 254;

        int paragraphs_width, paragraphs_height;
        get_paragraphs_size(painter, &paragraphs_width, &paragraphs_height);

        int total_width = qr_code_size + paragraphs_width + border;
        int total_height = MAX(paragraphs_height + border * 2, qr_code_size);

        update_vertices(painter, qr_code_size, total_width, total_height);

        painter->dialog_x = paint_state->pixel_width / 2 - total_width / 2;
        painter->dialog_y = paint_state->pixel_height / 2 - total_height / 2;
        painter->dialog_width = total_width;
        painter->dialog_height = total_height;

        for (int i = 0; i < VSX_N_ELEMENTS(painter->paragraphs); i++) {
                struct vsx_layout_paint_position *paragraph =
                        painter->paragraphs + i;

                paragraph->x += painter->dialog_x + qr_code_size;
                paragraph->y += (painter->dialog_y +
                                 painter->dialog_height / 2 -
                                 paragraphs_height / 2);
        }

        create_shadow(painter);

        painter->layout_dirty = false;
}

static void
prepare_cb(void *painter_data)
{
        struct vsx_invite_painter *painter = painter_data;

        ensure_layout(painter);
}

static void
set_uniforms(struct vsx_invite_painter *painter,
             const struct vsx_shader_data_program_data *program)
{
        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;
        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glUniformMatrix2fv(program->matrix_uniform,
                               1, /* count */
                               GL_FALSE, /* transpose */
                               paint_state->pixel_matrix);
        gl->glUniform2f(program->translation_uniform,
                        paint_state->pixel_translation[0],
                        paint_state->pixel_translation[1]);

}

static void
paint_shadow(struct vsx_invite_painter *painter)
{
        GLfloat translation[2];

        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        vsx_paint_state_offset_pixel_translation(paint_state,
                                                 painter->dialog_x,
                                                 painter->dialog_y,
                                                 translation);

        vsx_shadow_painter_paint(painter->toolbox->shadow_painter,
                                 painter->shadow,
                                 &painter->toolbox->shader_data,
                                 painter->toolbox->paint_state.pixel_matrix,
                                 translation);
}

static void
paint_cb(void *painter_data)
{
        struct vsx_invite_painter *painter = painter_data;

        if (painter->tex == 0 ||
            !vsx_shadow_painter_is_ready(painter->toolbox->shadow_painter))
                return;

        paint_shadow(painter);

        const struct vsx_shader_data *shader_data =
                &painter->toolbox->shader_data;

        const struct vsx_shader_data_program_data *program =
                shader_data->programs + VSX_SHADER_DATA_PROGRAM_TEXTURE;

        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glUseProgram(program->program);

        set_uniforms(painter, program);

        vsx_array_object_bind(painter->vao, gl);

        gl->glBindTexture(GL_TEXTURE_2D, painter->tex);

        vsx_gl_draw_range_elements(gl,
                                   GL_TRIANGLES,
                                   0, N_VERTICES - 1,
                                   N_QUADS * 6,
                                   GL_UNSIGNED_SHORT,
                                   NULL /* indices */);

        vsx_layout_paint_multiple(painter->paragraphs,
                                  VSX_N_ELEMENTS(painter->paragraphs));
}

static bool
handle_click(struct vsx_invite_painter *painter,
             const struct vsx_input_event *event)
{
        ensure_layout(painter);

        if (painter->tex == 0)
                return false;

        int x, y;

        struct vsx_toolbox *toolbox = painter->toolbox;

        vsx_paint_state_screen_to_pixel(&toolbox->paint_state,
                                        event->click.x, event->click.y,
                                        &x, &y);

        if (x < painter->dialog_x ||
            x >= painter->dialog_x + painter->dialog_width ||
            y < painter->dialog_y ||
            y >= painter->dialog_y + painter->dialog_height) {
                vsx_game_state_set_dialog(painter->game_state,
                                          VSX_DIALOG_NONE);
                return true;
        }

        const struct vsx_layout_paint_position *link =
                painter->paragraphs + VSX_N_ELEMENTS(painter->paragraphs) - 1;

        const struct vsx_layout_extents *extents =
                vsx_layout_get_logical_extents(link->layout);

        if (x >= link->x - extents->left &&
            x < link->x + extents->right &&
            y >= link->y - extents->top &&
            y < link->y + extents->bottom) {
                char url[VSX_ID_URL_ENCODED_SIZE + 1];

                vsx_id_url_encode(painter->id_in_texture, url);
                toolbox->share_link_callback(url, toolbox->share_link_data);

                return true;
        }

        return true;
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
                return handle_click(painter, event);
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

        vsx_list_remove(&painter->shadow_painter_ready_listener.link);
        vsx_list_remove(&painter->modified_listener.link);

        struct vsx_gl *gl = painter->toolbox->gl;

        if (painter->vao)
                vsx_array_object_free(painter->vao, gl);
        if (painter->vbo)
                gl->glDeleteBuffers(1, &painter->vbo);
        if (painter->element_buffer)
                gl->glDeleteBuffers(1, &painter->element_buffer);

        for (int i = 0; i < VSX_N_ELEMENTS(painter->paragraphs); i++) {
                if (painter->paragraphs[i].layout)
                        vsx_layout_free(painter->paragraphs[i].layout);
        }

        free_texture(painter);

        clear_shadow(painter);

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
