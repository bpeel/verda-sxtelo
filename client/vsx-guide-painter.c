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

#include "vsx-guide-painter.h"

#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <stdio.h>

#include "vsx-layout.h"
#include "vsx-monotonic.h"
#include "vsx-utf8.h"
#include "vsx-mipmap.h"
#include "vsx-guide.h"

#define CURSOR_SIZE 8

/* Time in microseconds to show the click cursor after a click */
#define CLICK_TIME (100 * 1000)

/* This is a translation of vsx_guide_animation that is easier to
 * handle at runtime.
 */
struct compiled_animation {
        /* Start time in microseconds */
        int start;
        /* Duration in microseconds */
        int duration;
        /* Thing to move. Either a letter number with the example
         * word, or MOVE_CURSOR to move the cursor.
         */
        int thing;
        /* Where to move to as an offset in pixels from the topleft of
         * the image space.
         */
        int dest_x, dest_y;

        enum vsx_guide_click_type click_type;
};

struct thing_pos {
        int num;
        int x, y;
        /* Link in the list in paint order */
        struct vsx_list link;
};

struct vsx_guide_painter {
        struct vsx_game_state *game_state;
        struct vsx_listener modified_listener;

        struct vsx_toolbox *toolbox;

        struct vsx_array_object *vao;
        GLuint vbo;

        bool layout_dirty;
        int paragraph_width;
        int dialog_x, dialog_y;
        int dialog_width, dialog_height;
        int image_x, image_y;
        int border;
        int image_size;
        int image_scissor_x, image_scissor_y;

        GLuint cursor_tex;
        struct vsx_image_loader_token *cursor_token;
        GLuint cursor_vbo;
        struct vsx_array_object *cursor_vao;

        GLuint image_tex;
        struct vsx_image_loader_token *image_token;

        const struct vsx_tile_texture_letter **example_letters;

        int n_tiles;

        /* “Compiled” versions of the animations that are easier to
         * process at runtime.
         */
        struct compiled_animation *animations;
        int n_animations;
        /* After this time in microseconds the animation will loop */
        int total_animation_duration;

        /* The position of each letter of the example word */
        struct thing_pos *letter_positions;
        /* The position of the cursor */
        struct thing_pos cursor_position;
        bool show_cursor;

        /* Whether we should currently show the click cursor */
        bool clicking;

        /* List of letters in paint order */
        struct vsx_list letter_list;

        int64_t start_time;

        /* Left arrow symbol, page text, right arrow symbol */
        struct vsx_layout_paint_position layouts[3];

        struct vsx_shadow_painter_shadow *shadow;
        struct vsx_listener shadow_painter_ready_listener;

        struct vsx_tile_tool_buffer *tile_buffer;
        struct vsx_listener tile_tool_ready_listener;
};

struct vertex {
        int16_t x, y;
        float s, t;
};

#define N_QUADS 1
#define N_VERTICES (N_QUADS * 4)

#define N_CURSOR_IMAGES 2
#define N_CURSOR_VERTICES (N_CURSOR_IMAGES * 4)

/* Max width of the explanation text in mm */
#define PARAGRAPH_WIDTH 40
/* Border size around the paragraphs in mm */
#define BORDER 5

#define CURSOR_X(size) (54 * (size) / 128)
#define CURSOR_Y(size) (32 * (size) / 128)

#define PARAGRAPH_FONT VSX_FONT_TYPE_LABEL

static void
free_image(struct vsx_guide_painter *painter)
{
        struct vsx_gl *gl = painter->toolbox->gl;

        if (painter->image_token) {
                vsx_image_loader_cancel(painter->image_token);
                painter->image_token = NULL;
        }

        if (painter->image_tex) {
                gl->glDeleteTextures(1, &painter->image_tex);
                painter->image_tex = 0;
        }
}

static void
image_loaded_cb(const struct vsx_image *image,
                struct vsx_error *error,
                void *data)
{
        struct vsx_guide_painter *painter = data;

        painter->image_token = NULL;

        if (error) {
                fprintf(stderr,
                        "error loading guide page image: %s\n",
                        error->message);
                return;
        }

        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glGenTextures(1, &painter->image_tex);

        gl->glBindTexture(GL_TEXTURE_2D, painter->image_tex);
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

        vsx_mipmap_load_image(image, gl, painter->image_tex);

        painter->toolbox->shell->queue_redraw_cb(painter->toolbox->shell);
}

static void
start_image_load(struct vsx_guide_painter *painter)
{
        const struct vsx_guide_page *page =
                vsx_guide_pages + vsx_game_state_get_page(painter->game_state);

        if (page->image == NULL)
                return;

        painter->image_token =
                vsx_image_loader_load(painter->toolbox->image_loader,
                                      page->image,
                                      image_loaded_cb,
                                      painter);
}

static void
handle_page_changed(struct vsx_guide_painter *painter)
{
        painter->layout_dirty = true;

        free_image(painter);

        const struct vsx_guide_page *page =
                vsx_guide_pages + vsx_game_state_get_page(painter->game_state);

        /* Reset the start time so that the animation on the new page
         * will start from zero.
         */
        painter->start_time = 0;

        /* If the page has an image then we’ll delay redrawing until
         * the image has loaded. It doesn’t matter if something else
         * causes a redraw in the meantime because the dialog will
         * just be drawn without the image. However this way we can
         * avoid a little flicker.
         */
        if (page->image) {
                start_image_load(painter);
        } else {
                struct vsx_shell_interface *shell = painter->toolbox->shell;
                shell->queue_redraw_cb(shell);
        }
}

static void
modified_cb(struct vsx_listener *listener,
            void *user_data)
{
        struct vsx_guide_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_guide_painter,
                                 modified_listener);
        const struct vsx_game_state_modified_event *event = user_data;
        struct vsx_shell_interface *shell = painter->toolbox->shell;

        switch (event->type) {
        case VSX_GAME_STATE_MODIFIED_TYPE_LANGUAGE:
                painter->layout_dirty = true;
                shell->queue_redraw_cb(shell);
                break;
        case VSX_GAME_STATE_MODIFIED_TYPE_PAGE:
                handle_page_changed(painter);
                break;
        default:
                break;
        }
}

static void
shadow_painter_ready_cb(struct vsx_listener *listener,
                        void *user_data)
{
        struct vsx_guide_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_guide_painter,
                                 shadow_painter_ready_listener);

        painter->toolbox->shell->queue_redraw_cb(painter->toolbox->shell);
}

static void
tile_tool_ready_cb(struct vsx_listener *listener,
                   void *user_data)
{
        struct vsx_guide_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_guide_painter,
                                 tile_tool_ready_listener);

        painter->toolbox->shell->queue_redraw_cb(painter->toolbox->shell);
}

static void
cursor_loaded_cb(const struct vsx_image *image,
               struct vsx_error *error,
               void *data)
{
        struct vsx_guide_painter *painter = data;

        painter->cursor_token = NULL;

        if (error) {
                fprintf(stderr,
                        "error loading cursor image: %s\n",
                        error->message);
                return;
        }

        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glGenTextures(1, &painter->cursor_tex);

        gl->glBindTexture(GL_TEXTURE_2D, painter->cursor_tex);
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

        vsx_mipmap_load_image(image, gl, painter->cursor_tex);

        painter->toolbox->shell->queue_redraw_cb(painter->toolbox->shell);
}

static void
clear_shadow(struct vsx_guide_painter *painter)
{
        if (painter->shadow == NULL)
                return;

        vsx_shadow_painter_free_shadow(painter->toolbox->shadow_painter,
                                       painter->shadow);
        painter->shadow = NULL;
}

static void
create_shadow(struct vsx_guide_painter *painter)
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
update_vertices(struct vsx_guide_painter *painter)
{
        const struct vsx_paint_state *paint_state =
                &painter->toolbox->paint_state;

        int x1 = (paint_state->pixel_width / 2 -
                  painter->dialog_width / 2);
        int y1 = (paint_state->pixel_height / 2 -
                  painter->dialog_height / 2);
        int x2 = x1 + painter->dialog_width;
        int y2 = y1 + painter->dialog_height;
        float s1 = ((painter->dialog_x - painter->image_x) /
                    (float) painter->image_size);
        float t1 = ((painter->dialog_y - painter->image_y) /
                    (float) painter->image_size);
        float s2 = ((painter->dialog_width + painter->dialog_x -
                     painter->image_x) /
                    (float) painter->image_size);
        float t2 = ((painter->dialog_height + painter->dialog_y -
                     painter->image_y) /
                    (float) painter->image_size);

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
create_buffer(struct vsx_guide_painter *painter)
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
}

static void
create_layouts(struct vsx_guide_painter *painter)
{
        /* Left/right arrows */
        for (int i = 0; i < 3; i += 2) {
                painter->layouts[i].r = 0.106f;
                painter->layouts[i].g = 0.561f;
                painter->layouts[i].b = 0.871f;

                painter->layouts[i].layout = vsx_layout_new(painter->toolbox);
                vsx_layout_set_text(painter->layouts[i].layout,
                                    i == 0 ? "<" : ">");
                vsx_layout_set_font(painter->layouts[i].layout,
                                    VSX_FONT_TYPE_SYMBOL);
                vsx_layout_prepare(painter->layouts[i].layout);
        }

        /* Page text */
        painter->layouts[1].layout = vsx_layout_new(painter->toolbox);
        vsx_layout_set_font(painter->layouts[1].layout, PARAGRAPH_FONT);
}

static void
create_cursor_buffer(struct vsx_guide_painter *painter)
{
        struct vsx_gl *gl = painter->toolbox->gl;

        struct vertex vertices[N_CURSOR_VERTICES];

        int dpi = painter->toolbox->paint_state.dpi;
        int cursor_size = CURSOR_SIZE * dpi * 10 / 254;
        int cursor_x = CURSOR_X(cursor_size);
        int cursor_y = CURSOR_Y(cursor_size);

        for (int i = 0; i < N_CURSOR_IMAGES; i++) {
                struct vertex *v = vertices + i * 4;
                float s1 = i / (float) N_CURSOR_IMAGES;
                float s2 = (i + 1.0f) / N_CURSOR_IMAGES;

                v->x = -cursor_x;
                v->y = -cursor_y;
                v->s = s1;
                v->t = 0.0f;
                v++;
                v->x = -cursor_x;
                v->y = -cursor_y + cursor_size;
                v->s = s1;
                v->t = 1.0f;
                v++;
                v->x = -cursor_x + cursor_size;
                v->y = -cursor_y;
                v->s = s2;
                v->t = 0.0f;
                v++;
                v->x = -cursor_x + cursor_size;
                v->y = -cursor_y + cursor_size;
                v->s = s2;
                v->t = 1.0f;
                v++;
        }

        gl->glGenBuffers(1, &painter->cursor_vbo);
        gl->glBindBuffer(GL_ARRAY_BUFFER, painter->cursor_vbo);
        gl->glBufferData(GL_ARRAY_BUFFER,
                         sizeof vertices, vertices,
                         GL_STATIC_DRAW);

        painter->cursor_vao = vsx_array_object_new(gl);

        vsx_array_object_set_attribute(painter->cursor_vao,
                                       gl,
                                       VSX_SHADER_DATA_ATTRIB_POSITION,
                                       2, /* size */
                                       GL_SHORT,
                                       false, /* normalized */
                                       sizeof (struct vertex),
                                       painter->cursor_vbo,
                                       offsetof(struct vertex, x));
        vsx_array_object_set_attribute(painter->cursor_vao,
                                       gl,
                                       VSX_SHADER_DATA_ATTRIB_TEX_COORD,
                                       2, /* size */
                                       GL_FLOAT,
                                       false, /* normalized */
                                       sizeof (struct vertex),
                                       painter->cursor_vbo,
                                       offsetof(struct vertex, s));
}

static void
free_animations(struct vsx_guide_painter *painter)
{
        vsx_free(painter->animations);
        painter->animations = NULL;
}

static void
compile_animations(struct vsx_guide_painter *painter,
                   const struct vsx_guide_page *page)
{
        free_animations(painter);

        painter->n_animations = page->n_animations;

        if (page->n_animations == 0)
                return;

        painter->animations = vsx_alloc(sizeof (struct compiled_animation) *
                                        page->n_animations);

        int dpi = painter->toolbox->paint_state.dpi;

        int total_duration = 0;

        for (int i = 0; i < page->n_animations; i++) {
                struct compiled_animation *dst = painter->animations + i;
                const struct vsx_guide_animation *src = page->animations + i;

                dst->thing = src->thing;
                dst->click_type = src->click_type;

                struct thing_pos *pos = (src->thing == VSX_GUIDE_MOVE_CURSOR ?
                                         &painter->cursor_position :
                                         painter->letter_positions +
                                         src->thing);

                if (src->start_after == 0) {
                        dst->start = 0;
                } else {
                        const struct compiled_animation *before =
                                painter->animations + i + src->start_after;
                        dst->start = before->start + before->duration;
                }

                if (src->speed > 0) {
                        float dx = pos->x - src->dest_x;
                        float dy = pos->y - src->dest_y;
                        float d = sqrtf((dx * dx) + (dy * dy));

                        dst->duration = d * 1e6f / src->speed;
                } else {
                        dst->duration = 0;
                }

                if (dst->start + dst->duration > total_duration)
                        total_duration = dst->start + dst->duration;

                pos->x = src->dest_x;
                pos->y = src->dest_y;

                dst->dest_x = src->dest_x * dpi * 10 / 254;
                dst->dest_y = src->dest_y * dpi * 10 / 254;
        }

        painter->total_animation_duration =
                total_duration > 0 ?
                total_duration + 1000 * 1000:
                0;
}

static void *
create_cb(struct vsx_game_state *game_state,
          struct vsx_toolbox *toolbox)
{
        struct vsx_guide_painter *painter = vsx_calloc(sizeof *painter);

        painter->game_state = game_state;
        painter->toolbox = toolbox;

        /* Convert the measurements from mm to pixels */
        int dpi = toolbox->paint_state.dpi;
        painter->paragraph_width = PARAGRAPH_WIDTH * dpi * 10 / 254;
        painter->border = BORDER * dpi * 10 / 254;
        painter->image_size = VSX_GUIDE_IMAGE_SIZE * dpi * 10 / 254;

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

        painter->tile_tool_ready_listener.notify = tile_tool_ready_cb;
        vsx_signal_add(vsx_tile_tool_get_ready_signal(toolbox->tile_tool),
                       &painter->tile_tool_ready_listener);

        painter->cursor_token =
                vsx_image_loader_load(toolbox->image_loader,
                                      "cursor.mpng",
                                      cursor_loaded_cb,
                                      painter);

        create_cursor_buffer(painter);

        create_layouts(painter);

        start_image_load(painter);

        return painter;
}

static void
fb_size_changed_cb(void *painter_data)
{
        struct vsx_guide_painter *painter = painter_data;

        painter->layout_dirty = true;
}

static void
update_paragraph(struct vsx_guide_painter *painter,
                 const struct vsx_guide_page *page)
{
        struct vsx_layout *layout = painter->layouts[1].layout;

        enum vsx_text_language language =
                vsx_game_state_get_language(painter->game_state);
        vsx_layout_set_text(layout, vsx_text_get(language, page->text));

        vsx_layout_set_width(layout, painter->paragraph_width);

        vsx_layout_prepare(layout);
}

static void
free_tile_buffer(struct vsx_guide_painter *painter)
{
        if (painter->tile_buffer) {
                vsx_tile_tool_free_buffer(painter->tile_buffer);
                painter->tile_buffer = NULL;
        }
}

static void
update_tile_buffer(struct vsx_guide_painter *painter,
                   const struct vsx_guide_page *page)
{
        free_tile_buffer(painter);

        int dpi = painter->toolbox->paint_state.dpi;
        int tile_size = page->tile_size * dpi * 10 / 254;

        painter->tile_buffer =
                vsx_tile_tool_create_buffer(painter->toolbox->tile_tool,
                                            tile_size);
}

static void
free_letters(struct vsx_guide_painter *painter)
{
        vsx_free(painter->example_letters);
        painter->example_letters = NULL;
        vsx_free(painter->letter_positions);
        painter->letter_positions = NULL;
}

static int
get_word_length(const char *word)
{
        int length = 0;

        while (*word) {
                length++;
                word = vsx_utf8_next(word);
        }

        return length;
}

static void
create_letters(struct vsx_guide_painter *painter,
               const char *word)
{
        int length = get_word_length(word);

        painter->example_letters =
                vsx_alloc(sizeof (const struct vsx_tile_texture_letter *) *
                          length);
        painter->letter_positions =
                vsx_alloc(sizeof *painter->letter_positions * length);

        vsx_list_init(&painter->letter_list);

        const char *p = word;

        for (int i = 0; i < length; i++) {
                struct thing_pos *pos = painter->letter_positions + i;

                pos->num = i;
                vsx_list_insert(painter->letter_list.prev, &pos->link);

                assert(*p);

                painter->example_letters[i] =
                        vsx_tile_texture_find_letter(vsx_utf8_get_char(p));

                assert(painter->example_letters[i]);

                p = vsx_utf8_next(p);
        }

        assert(*p == '\0');

        painter->n_tiles = length;
}

static void
update_letters(struct vsx_guide_painter *painter,
               const struct vsx_guide_page *page)
{
        free_letters(painter);

        if (!page->has_tiles) {
                vsx_list_init(&painter->letter_list);
                painter->n_tiles = 0;
                return;
        }

        enum vsx_text_language language =
                vsx_game_state_get_language(painter->game_state);

        create_letters(painter, vsx_text_get(language, page->example_word));
}

static void
update_arrow_positions(struct vsx_guide_painter *painter)
{
        for (int i = 0; i < 2; i++) {
                struct vsx_layout_paint_position *pos =
                        painter->layouts +
                        (i == 0 ? 0 : VSX_N_ELEMENTS(painter->layouts) - 1);
                const struct vsx_layout_extents *extents =
                        vsx_layout_get_logical_extents(pos->layout);

                pos->x = (painter->dialog_x +
                          painter->border / 2 -
                          extents->right / 2);

                if (i > 0)
                        pos->x += painter->dialog_width - painter->border;

                pos->y = (painter->dialog_y +
                          painter->dialog_height / 2 +
                          extents->top / 2);
        }
}

static void
ensure_layout(struct vsx_guide_painter *painter)
{
        if (!painter->layout_dirty)
                return;

        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        vsx_paint_state_ensure_layout(paint_state);

        const struct vsx_guide_page *page =
                vsx_guide_pages +
                vsx_game_state_get_page(painter->game_state);

        update_paragraph(painter, page);
        update_tile_buffer(painter, page);

        painter->show_cursor = page->show_cursor;

        const struct vsx_layout_extents *extents =
                vsx_layout_get_logical_extents(painter->layouts[1].layout);

        int paragraph_height = extents->top + extents->bottom;

        int total_width = (painter->paragraph_width +
                           painter->border * 3 +
                           painter->image_size);
        int total_height = (MAX(paragraph_height, painter->image_size) +
                            painter->border * 2);

        painter->dialog_x = paint_state->pixel_width / 2 - total_width / 2;
        painter->dialog_y = paint_state->pixel_height / 2 - total_height / 2;
        painter->dialog_width = total_width;
        painter->dialog_height = total_height;

        painter->image_x = painter->dialog_x + painter->border;
        painter->image_y = (painter->dialog_y +
                            painter->dialog_height / 2 -
                            painter->image_size / 2);

        if (paint_state->board_rotated) {
                painter->image_scissor_x = (paint_state->width -
                                            painter->image_y -
                                            painter->image_size);
                painter->image_scissor_y = (paint_state->height -
                                            painter->image_x -
                                            painter->image_size);
        } else {
                painter->image_scissor_x = painter->image_x;
                painter->image_scissor_y = (paint_state->height -
                                            painter->image_y -
                                            painter->image_size);
        }

        painter->layouts[1].x = (painter->image_x +
                                 painter->image_size +
                                 painter->border);
        painter->layouts[1].y = (painter->dialog_y +
                                 painter->dialog_height / 2 -
                                 paragraph_height / 2 +
                                 extents->top);

        update_arrow_positions(painter);

        update_vertices(painter);

        create_shadow(painter);

        update_letters(painter, page);

        compile_animations(painter, page);

        painter->layout_dirty = false;
}

static void
prepare_cb(void *painter_data)
{
        struct vsx_guide_painter *painter = painter_data;

        ensure_layout(painter);
}

static void
paint_background(struct vsx_guide_painter *painter)
{
        const struct vsx_shader_data *shader_data =
                &painter->toolbox->shader_data;

        const struct vsx_shader_data_program_data *program =
                shader_data->programs + (painter->image_tex ?
                                         VSX_SHADER_DATA_PROGRAM_TEXTURE :
                                         VSX_SHADER_DATA_PROGRAM_SOLID);

        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glUseProgram(program->program);

        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        gl->glUniformMatrix2fv(program->matrix_uniform,
                               1, /* count */
                               GL_FALSE, /* transpose */
                               paint_state->pixel_matrix);
        gl->glUniform2f(program->translation_uniform,
                        paint_state->pixel_translation[0],
                        paint_state->pixel_translation[1]);

        vsx_array_object_bind(painter->vao, gl);

        if (painter->image_tex)
                gl->glBindTexture(GL_TEXTURE_2D, painter->image_tex);
        else
                gl->glUniform3f(program->color_uniform, 1.0f, 1.0f, 1.0f);

        gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, N_VERTICES);
}

static void
paint_shadow(struct vsx_guide_painter *painter)
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

static int
get_elapsed_time(struct vsx_guide_painter *painter)
{
        int64_t now = vsx_monotonic_get();

        if (painter->start_time == 0 ||
            painter->total_animation_duration == 0) {
                painter->start_time = now;
                return 0;
        } else {
                return ((now - painter->start_time) %
                        painter->total_animation_duration);
        }
}

static void
update_animations(struct vsx_guide_painter *painter)
{
        int elapsed_time = get_elapsed_time(painter);

        painter->clicking = false;

        for (int i = 0; i < painter->n_animations; i++) {
                const struct compiled_animation *animation =
                        painter->animations + i;

                if (elapsed_time < animation->start)
                        break;

                struct thing_pos *pos =
                        (animation->thing == VSX_GUIDE_MOVE_CURSOR ?
                         &painter->cursor_position :
                         painter->letter_positions + animation->thing);

                if (elapsed_time >= animation->start + animation->duration) {
                        pos->x = animation->dest_x;
                        pos->y = animation->dest_y;
                        continue;
                }

                int t = elapsed_time - animation->start;
                pos->x += ((animation->dest_x - pos->x) * t /
                           animation->duration);
                pos->y += ((animation->dest_y - pos->y) * t /
                           animation->duration);

                if (animation->thing != VSX_GUIDE_MOVE_CURSOR) {
                        /* Move this letter to the end of the letter
                         * list so that it will be drawn on top.
                         */
                        vsx_list_remove(&pos->link);
                        vsx_list_insert(painter->letter_list.prev, &pos->link);
                }

                switch (animation->click_type) {
                case VSX_GUIDE_CLICK_TYPE_NONE:
                        break;

                case VSX_GUIDE_CLICK_TYPE_SHORT:
                        if (t < CLICK_TIME)
                                painter->clicking = true;
                        break;

                case VSX_GUIDE_CLICK_TYPE_DRAG:
                        painter->clicking = true;
                        break;
                }
        }
}

static void
update_tiles(struct vsx_guide_painter *painter)
{
        vsx_tile_tool_begin_update(painter->tile_buffer, painter->n_tiles);

        struct thing_pos *pos;

        vsx_list_for_each(pos, &painter->letter_list, link) {
                const struct vsx_tile_texture_letter *letter =
                        painter->example_letters[pos->num];

                vsx_tile_tool_add_tile(painter->tile_buffer,
                                       painter->image_x + pos->x,
                                       painter->image_y + pos->y,
                                       letter);
        }

        vsx_tile_tool_end_update(painter->tile_buffer);
}

static void
draw_cursor(struct vsx_guide_painter *painter,
            int x,
            int y,
            bool clicking)
{
        if (painter->cursor_tex == 0)
                return;

        const struct vsx_shader_data *shader_data =
                &painter->toolbox->shader_data;

        const struct vsx_shader_data_program_data *program =
                shader_data->programs + VSX_SHADER_DATA_PROGRAM_TEXTURE;

        struct vsx_gl *gl = painter->toolbox->gl;

        struct vsx_paint_state *paint_state =
                &painter->toolbox->paint_state;

        gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl->glEnable(GL_BLEND);

        gl->glUseProgram(program->program);

        gl->glUniformMatrix2fv(program->matrix_uniform,
                               1, /* count */
                               GL_FALSE, /* transpose */
                               paint_state->pixel_matrix);

        GLfloat translation[2];

        vsx_paint_state_offset_pixel_translation(paint_state,
                                                 x, y,
                                                 translation);
        gl->glUniform2f(program->translation_uniform,
                        translation[0],
                        translation[1]);

        gl->glBindTexture(GL_TEXTURE_2D, painter->cursor_tex);

        vsx_array_object_bind(painter->cursor_vao, gl);

        gl->glDrawArrays(GL_TRIANGLE_STRIP,
                         clicking ? 4 : 0,
                         4);

        gl->glDisable(GL_BLEND);
}

static void
paint_layouts(struct vsx_guide_painter *painter)
{
        int first_layout = 0, n_layouts = VSX_N_ELEMENTS(painter->layouts);

        int page = vsx_game_state_get_page(painter->game_state);

        if (page <= 0) {
                first_layout++;
                n_layouts--;
        }

        if (page >= VSX_GUIDE_N_PAGES - 1)
                n_layouts--;

        vsx_layout_paint_multiple(painter->layouts + first_layout, n_layouts);
}

static void
paint_things(struct vsx_guide_painter *painter)
{
        if (painter->n_tiles <= 0 && !painter->show_cursor)
                return;

        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;
        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glEnable(GL_SCISSOR_TEST);
        gl->glScissor(painter->image_scissor_x,
                      painter->image_scissor_y,
                      painter->image_size,
                      painter->image_size);

        if (painter->n_tiles > 0) {
                update_tiles(painter);
                vsx_tile_tool_paint(painter->tile_buffer,
                                    &painter->toolbox->shader_data,
                                    paint_state->pixel_matrix,
                                    paint_state->pixel_translation);
        }

        if (painter->show_cursor) {
                draw_cursor(painter,
                            painter->cursor_position.x + painter->image_x,
                            painter->cursor_position.y + painter->image_y,
                            painter->clicking);
        }

        gl->glDisable(GL_SCISSOR_TEST);
}

static void
paint_cb(void *painter_data)
{
        struct vsx_guide_painter *painter = painter_data;

        if (!vsx_tile_tool_is_ready(painter->toolbox->tile_tool) ||
            !vsx_shadow_painter_is_ready(painter->toolbox->shadow_painter))
                return;

        update_animations(painter);

        paint_shadow(painter);

        paint_background(painter);

        paint_things(painter);

        paint_layouts(painter);

        if (painter->total_animation_duration > 0) {
                struct vsx_shell_interface *shell = painter->toolbox->shell;
                shell->queue_redraw_cb(shell);
        }
}

static bool
handle_click(struct vsx_guide_painter *painter,
             const struct vsx_input_event *event)
{
        ensure_layout(painter);

        int x, y;

        struct vsx_toolbox *toolbox = painter->toolbox;

        vsx_paint_state_screen_to_pixel(&toolbox->paint_state,
                                        event->click.x, event->click.y,
                                        &x, &y);

        if (x < painter->dialog_x ||
            x >= painter->dialog_x + painter->dialog_width ||
            y < painter->dialog_y ||
            y >= painter->dialog_y + painter->dialog_height) {
                vsx_game_state_close_dialog(painter->game_state);
                return true;
        }

        int current_page = vsx_game_state_get_page(painter->game_state);

        if (x >= painter->dialog_x + painter->dialog_width / 2) {
                if (current_page < VSX_GUIDE_N_PAGES - 1) {
                        vsx_game_state_set_page(painter->game_state,
                                                current_page + 1);
                }
        } else if (current_page > 0) {
                vsx_game_state_set_page(painter->game_state,
                                        current_page - 1);
        }

        return true;
}

static bool
input_event_cb(void *painter_data,
               const struct vsx_input_event *event)
{
        struct vsx_guide_painter *painter = painter_data;

        switch (event->type) {
        case VSX_INPUT_EVENT_TYPE_DRAG_START:
        case VSX_INPUT_EVENT_TYPE_DRAG:
        case VSX_INPUT_EVENT_TYPE_ZOOM_START:
        case VSX_INPUT_EVENT_TYPE_ZOOM:
                return true;

        case VSX_INPUT_EVENT_TYPE_CLICK:
                return handle_click(painter, event);
        }

        return false;
}

static void
free_cb(void *painter_data)
{
        struct vsx_guide_painter *painter = painter_data;

        vsx_list_remove(&painter->shadow_painter_ready_listener.link);
        vsx_list_remove(&painter->tile_tool_ready_listener.link);
        vsx_list_remove(&painter->modified_listener.link);

        struct vsx_gl *gl = painter->toolbox->gl;

        if (painter->vao)
                vsx_array_object_free(painter->vao, gl);
        if (painter->vbo)
                gl->glDeleteBuffers(1, &painter->vbo);

        for (int i = 0; i < VSX_N_ELEMENTS(painter->layouts); i++) {
                if (painter->layouts[i].layout)
                        vsx_layout_free(painter->layouts[i].layout);
        }

        if (painter->cursor_token)
                vsx_image_loader_cancel(painter->cursor_token);
        if (painter->cursor_tex)
                gl->glDeleteTextures(1, &painter->cursor_tex);

        if (painter->cursor_vbo)
                gl->glDeleteBuffers(1, &painter->cursor_vbo);
        if (painter->cursor_vao)
                vsx_array_object_free(painter->cursor_vao, gl);

        free_image(painter);
        free_tile_buffer(painter);
        free_letters(painter);
        free_animations(painter);

        clear_shadow(painter);

        vsx_free(painter);
}

const struct vsx_painter
vsx_guide_painter = {
        .create_cb = create_cb,
        .fb_size_changed_cb = fb_size_changed_cb,
        .prepare_cb = prepare_cb,
        .paint_cb = paint_cb,
        .input_event_cb = input_event_cb,
        .free_cb = free_cb,
};
