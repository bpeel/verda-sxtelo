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

/* Size of the area reserved for the animations or images size in mm */
#define IMAGE_SIZE 25

#define MOVE_CURSOR EXAMPLE_WORD_LENGTH

#define EXAMPLE_WORD_LENGTH 5

/* Speed the cursor is moved at in mm/s */
#define CURSOR_SPEED 20
/* Speed that a tile moves to jump into place when it is clicked on */
#define JUMP_SPEED 40

/* Size of a tile in mm */
#define TILE_SIZE (IMAGE_SIZE / 5)

#define CURSOR_SIZE 8

/* Time in microseconds to show the click cursor after a click */
#define CLICK_TIME (100 * 1000)

enum animation_click_type {
        ANIMATION_CLICK_TYPE_NONE,
        /* Show a short click at the start of the animation */
        ANIMATION_CLICK_TYPE_SHORT,
        /* Show the click icon for the duration of the animation */
        ANIMATION_CLICK_TYPE_DRAG,
};

/* This is a translation of struct animation that is easier to handle
 * at runtime.
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

        enum animation_click_type click_type;
};

struct animation {
        /* Offset of the animation after which this animation should
         * start. Ie, -1 is the animation before this one, etc. Zero
         * means to start immediately.
         */
        int start_after;
        /* The speed of the movement in mm/s, or zero to displace the
         * thing instantaneously.
         */
        int speed;
        /* Thing to move. Either a letter number within the example
         * word, or MOVE_CURSOR to move the cursor.
         */
        int thing;
        /* Where to move to as an offset in mm from the topleft of
         * the image space.
         */
        int dest_x, dest_y;

        enum animation_click_type click_type;
};

static const struct animation
animations[] = {
        /* Zero-length animations to initialise the positions */
        { .thing = 0, .dest_x = 19, .dest_y = 2 },
        { .thing = 1, .dest_x = 3, .dest_y = 12 },
        { .thing = 2, .dest_x = 17, .dest_y = 10 },
        { .thing = 3, .dest_x = 3, .dest_y = 2 },
        { .thing = 4, .dest_x = 9, .dest_y = 6 },
        {
                .thing = MOVE_CURSOR,
                .dest_x = IMAGE_SIZE / 2,
                .dest_y = IMAGE_SIZE / 2,
        },

        /* Move the cursor to the first letter */
        {
                .thing = MOVE_CURSOR,
                .dest_x = 22, .dest_y = 5,
                .start_after = 0,
                .speed = CURSOR_SPEED,
        },
        /* Move the cursor and the first letter into position */
        {
                .thing = MOVE_CURSOR,
                .dest_x = 3, .dest_y = IMAGE_SIZE - TILE_SIZE + 3,
                .start_after = -1,
                .speed = CURSOR_SPEED,
                .click_type = ANIMATION_CLICK_TYPE_DRAG,
        },
        {
                .thing = 0,
                .dest_x = 0, .dest_y = IMAGE_SIZE - TILE_SIZE,
                .start_after = -2,
                .speed = CURSOR_SPEED,
        },

        /* Move the cursor to the second letter */
        {
                .thing = MOVE_CURSOR,
                .dest_x = 6, .dest_y = 15,
                .start_after = -1,
                .speed = CURSOR_SPEED,
        },
        /* Make the tile jump into place */
        {
                .thing = 1,
                .dest_x = TILE_SIZE, .dest_y = IMAGE_SIZE - TILE_SIZE,
                .start_after = -1,
                .speed = JUMP_SPEED,
        },

        /* Move the cursor to the third letter */
        {
                .thing = MOVE_CURSOR,
                .dest_x = 20, .dest_y = 13,
                .start_after = -2,
                .speed = CURSOR_SPEED,
                .click_type = ANIMATION_CLICK_TYPE_SHORT,
        },
        /* Third tile jump into place */
        {
                .thing = 2,
                .dest_x = TILE_SIZE * 2, .dest_y = IMAGE_SIZE - TILE_SIZE,
                .start_after = -1,
                .speed = JUMP_SPEED,
        },

        /* Move the cursor to the fourth letter */
        {
                .thing = MOVE_CURSOR,
                .dest_x = 6, .dest_y = 5,
                .start_after = -2,
                .speed = CURSOR_SPEED,
                .click_type = ANIMATION_CLICK_TYPE_SHORT,
        },
        /* Fourth tile jump into place */
        {
                .thing = 3,
                .dest_x = TILE_SIZE * 3, .dest_y = IMAGE_SIZE - TILE_SIZE,
                .start_after = -1,
                .speed = JUMP_SPEED,
        },

        /* Move the cursor to the fifth letter */
        {
                .thing = MOVE_CURSOR,
                .dest_x = 12, .dest_y = 9,
                .start_after = -2,
                .speed = CURSOR_SPEED,
                .click_type = ANIMATION_CLICK_TYPE_SHORT,
        },
        /* Fourth tile jump into place */
        {
                .thing = 4,
                .dest_x = TILE_SIZE * 4, .dest_y = IMAGE_SIZE - TILE_SIZE,
                .start_after = -1,
                .speed = JUMP_SPEED,
        },

        /* Move the cursor back to the center */
        {
                .thing = MOVE_CURSOR,
                .start_after = -2,
                .dest_x = IMAGE_SIZE / 2, .dest_y = IMAGE_SIZE / 2,
                .speed = CURSOR_SPEED,
                .click_type = ANIMATION_CLICK_TYPE_SHORT,
        },
};

#define N_ANIMATIONS VSX_N_ELEMENTS(animations)

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
        int dialog_x, dialog_y;
        int dialog_width, dialog_height;
        int image_x, image_y;
        int border;
        int image_size;
        int tile_size;

        GLuint cursor_tex;
        struct vsx_image_loader_token *cursor_token;
        GLuint cursor_vbo;
        struct vsx_array_object *cursor_vao;

        const struct vsx_tile_texture_letter *
        example_letters[EXAMPLE_WORD_LENGTH];

        /* “Compiled” versions of the animations that are easier to
         * process at runtime.
         */
        struct compiled_animation animations[N_ANIMATIONS];
        /* After this time in microseconds the animation will loop */
        int total_animation_duration;

        /* The position of each letter of the example word, plus the
         * position of the cursor.
         */
        struct thing_pos thing_positions[EXAMPLE_WORD_LENGTH + 1];

        /* Whether we should currently show the click cursor */
        bool clicking;

        /* List of things in paint order */
        struct vsx_list thing_list;

        int64_t start_time;

        struct vsx_layout_paint_position paragraph;

        struct vsx_shadow_painter_shadow *shadow;
        struct vsx_listener shadow_painter_ready_listener;

        struct vsx_tile_tool_buffer *tile_buffer;
        struct vsx_listener tile_tool_ready_listener;

        struct vsx_signal redraw_needed_signal;
};

struct vertex {
        int16_t x, y;
};

struct cursor_vertex {
        int16_t x, y;
        uint8_t s, t;
};

#define N_QUADS 1
#define N_VERTICES (N_QUADS * 4)

#define N_CURSOR_IMAGES 2
#define N_CURSOR_VERTICES (N_CURSOR_IMAGES * 4)

/* Max width of the explanation text in mm */
#define PARAGRAPH_WIDTH 40
/* Border size around the paragraphs in mm */
#define BORDER 4

#define CURSOR_X(size) (54 * (size) / 128)
#define CURSOR_Y(size) (32 * (size) / 128)

#define PARAGRAPH_FONT VSX_FONT_TYPE_LABEL

static void
modified_cb(struct vsx_listener *listener,
            void *user_data)
{
        struct vsx_guide_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_guide_painter,
                                 modified_listener);
        const struct vsx_game_state_modified_event *event = user_data;

        switch (event->type) {
        case VSX_GAME_STATE_MODIFIED_TYPE_LANGUAGE:
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
        struct vsx_guide_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_guide_painter,
                                 shadow_painter_ready_listener);

        vsx_signal_emit(&painter->redraw_needed_signal, NULL);
}

static void
tile_tool_ready_cb(struct vsx_listener *listener,
                   void *user_data)
{
        struct vsx_guide_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_guide_painter,
                                 tile_tool_ready_listener);

        vsx_signal_emit(&painter->redraw_needed_signal, NULL);
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

        vsx_signal_emit(&painter->redraw_needed_signal, NULL);
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
update_vertices(struct vsx_guide_painter *painter,
                int total_width,
                int total_height)
{
        const struct vsx_paint_state *paint_state =
                &painter->toolbox->paint_state;

        int x1 = paint_state->pixel_width / 2 - total_width / 2;
        int y1 = paint_state->pixel_height / 2 - total_height / 2;
        int x2 = x1 + total_width;
        int y2 = y1 + total_height;

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
        v++;

        v->x = x1;
        v->y = y2;
        v++;

        v->x = x2;
        v->y = y1;
        v++;

        v->x = x2;
        v->y = y2;
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
                                       0, /* divisor */
                                       painter->vbo,
                                       offsetof(struct vertex, x));
}

static void
create_layout(struct vsx_guide_painter *painter)
{
        struct vsx_layout *layout =
                vsx_layout_new(painter->toolbox);

        vsx_layout_set_font(layout, PARAGRAPH_FONT);

        painter->paragraph.layout = layout;
}

static void
create_cursor_buffer(struct vsx_guide_painter *painter)
{
        struct vsx_gl *gl = painter->toolbox->gl;

        struct cursor_vertex vertices[N_CURSOR_VERTICES];

        int dpi = painter->toolbox->paint_state.dpi;
        int cursor_size = CURSOR_SIZE * dpi * 10 / 254;
        int cursor_x = CURSOR_X(cursor_size);
        int cursor_y = CURSOR_Y(cursor_size);

        for (int i = 0; i < N_CURSOR_IMAGES; i++) {
                struct cursor_vertex *v = vertices + i * 4;
                uint8_t s1 = i * 255 / N_CURSOR_IMAGES;
                uint8_t s2 = (i + 1) * 255 / N_CURSOR_IMAGES;

                v->x = -cursor_x;
                v->y = -cursor_y;
                v->s = s1;
                v->t = 0;
                v++;
                v->x = -cursor_x;
                v->y = -cursor_y + cursor_size;
                v->s = s1;
                v->t = 255;
                v++;
                v->x = -cursor_x + cursor_size;
                v->y = -cursor_y;
                v->s = s2;
                v->t = 0;
                v++;
                v->x = -cursor_x + cursor_size;
                v->y = -cursor_y + cursor_size;
                v->s = s2;
                v->t = 255;
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
                                       sizeof (struct cursor_vertex),
                                       0, /* divisor */
                                       painter->cursor_vbo,
                                       offsetof(struct cursor_vertex, x));
        vsx_array_object_set_attribute(painter->cursor_vao,
                                       gl,
                                       VSX_SHADER_DATA_ATTRIB_TEX_COORD,
                                       2, /* size */
                                       GL_UNSIGNED_BYTE,
                                       true, /* normalized */
                                       sizeof (struct cursor_vertex),
                                       0, /* divisor */
                                       painter->cursor_vbo,
                                       offsetof(struct cursor_vertex, s));
}

static void
convert_animations(struct vsx_guide_painter *painter)
{
        int dpi = painter->toolbox->paint_state.dpi;

        int total_duration = 0;

        for (int i = 0; i < N_ANIMATIONS; i++) {
                struct compiled_animation *dst = painter->animations + i;
                const struct animation *src = animations + i;

                dst->thing = src->thing;
                dst->click_type = src->click_type;

                if (src->start_after == 0) {
                        dst->start = 0;
                } else {
                        const struct compiled_animation *before =
                                painter->animations + i + src->start_after;
                        dst->start = before->start + before->duration;
                }

                if (src->speed > 0) {
                        float dx = (painter->thing_positions[dst->thing].x -
                                    src->dest_x);
                        float dy = (painter->thing_positions[dst->thing].y -
                                    src->dest_y);
                        float d = sqrtf((dx * dx) + (dy * dy));

                        dst->duration = d * 1e6f / src->speed;
                } else {
                        dst->duration = 0;
                }

                if (dst->start + dst->duration > total_duration)
                        total_duration = dst->start + dst->duration;

                painter->thing_positions[dst->thing].x = src->dest_x;
                painter->thing_positions[dst->thing].y = src->dest_y;

                dst->dest_x = src->dest_x * dpi * 10 / 254;
                dst->dest_y = src->dest_y * dpi * 10 / 254;
        }

        painter->total_animation_duration = total_duration + 1000 * 1000;
}

static void
init_positions(struct vsx_guide_painter *painter)
{
        vsx_list_init(&painter->thing_list);

        for (int i = 0; i < VSX_N_ELEMENTS(painter->thing_positions); i++) {
                struct thing_pos *pos = painter->thing_positions + i;

                pos->num = i;
                vsx_list_insert(painter->thing_list.prev, &pos->link);
        }
}

static void *
create_cb(struct vsx_game_state *game_state,
          struct vsx_toolbox *toolbox)
{
        struct vsx_guide_painter *painter = vsx_calloc(sizeof *painter);

        vsx_signal_init(&painter->redraw_needed_signal);

        painter->game_state = game_state;
        painter->toolbox = toolbox;

        init_positions(painter);

        /* Convert the measurements from mm to pixels */
        int dpi = toolbox->paint_state.dpi;
        painter->border = BORDER * dpi * 10 / 254;
        painter->image_size = IMAGE_SIZE * dpi * 10 / 254;
        painter->tile_size = TILE_SIZE * dpi * 10 / 254;

        convert_animations(painter);

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

        struct vsx_tile_tool *tile_tool = toolbox->tile_tool;

        painter->tile_buffer =
                vsx_tile_tool_create_buffer(tile_tool, painter->tile_size);

        painter->tile_tool_ready_listener.notify = tile_tool_ready_cb;
        vsx_signal_add(vsx_tile_tool_get_ready_signal(tile_tool),
                       &painter->tile_tool_ready_listener);

        painter->cursor_token =
                vsx_image_loader_load(toolbox->image_loader,
                                      "cursor.mpng",
                                      cursor_loaded_cb,
                                      painter);

        create_cursor_buffer(painter);

        create_layout(painter);

        return painter;
}

static void
fb_size_changed_cb(void *painter_data)
{
        struct vsx_guide_painter *painter = painter_data;

        painter->layout_dirty = true;
}

static void
update_paragraph(struct vsx_guide_painter *painter)
{
        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;
        int paragraph_width = PARAGRAPH_WIDTH * paint_state->dpi * 10 / 254;

        enum vsx_text_language language =
                vsx_game_state_get_language(painter->game_state);
        vsx_layout_set_text(painter->paragraph.layout,
                            vsx_text_get(language,
                                         VSX_TEXT_GUIDE_MOVE_WORD));

        vsx_layout_set_width(painter->paragraph.layout, paragraph_width);

        vsx_layout_prepare(painter->paragraph.layout);
}

static void
update_example_letters(struct vsx_guide_painter *painter)
{
        enum vsx_text_language language =
                vsx_game_state_get_language(painter->game_state);
        const char *p = vsx_text_get(language, VSX_TEXT_GUIDE_EXAMPLE_WORD);

        for (int i = 0; i < EXAMPLE_WORD_LENGTH; i++, p = vsx_utf8_next(p)) {
                assert(*p);

                painter->example_letters[i] =
                        vsx_tile_texture_find_letter(vsx_utf8_get_char(p));

                assert(painter->example_letters[i]);
        }

        assert(*p == '\0');
}

static void
ensure_layout(struct vsx_guide_painter *painter)
{
        if (!painter->layout_dirty)
                return;

        struct vsx_paint_state *paint_state = &painter->toolbox->paint_state;

        vsx_paint_state_ensure_layout(paint_state);

        update_paragraph(painter);

        const struct vsx_layout_extents *extents =
                vsx_layout_get_logical_extents(painter->paragraph.layout);

        int paragraph_height = extents->top + extents->bottom;

        int total_width = (extents->right +
                           painter->border * 3 +
                           painter->image_size);
        int total_height = (MAX(paragraph_height, painter->image_size) +
                            painter->border * 2);

        update_vertices(painter, total_width, total_height);

        painter->dialog_x = paint_state->pixel_width / 2 - total_width / 2;
        painter->dialog_y = paint_state->pixel_height / 2 - total_height / 2;
        painter->dialog_width = total_width;
        painter->dialog_height = total_height;

        painter->image_x = painter->dialog_x + painter->border;
        painter->image_y = (painter->dialog_y +
                            painter->dialog_height / 2 -
                            painter->image_size / 2);

        painter->paragraph.x = (painter->image_x +
                                painter->image_size +
                                painter->border);
        painter->paragraph.y = (painter->dialog_y +
                                painter->dialog_height / 2 -
                                paragraph_height / 2 +
                                extents->top);

        create_shadow(painter);

        update_example_letters(painter);

        painter->layout_dirty = false;
}

static void
prepare_cb(void *painter_data)
{
        struct vsx_guide_painter *painter = painter_data;

        ensure_layout(painter);
}

static void
set_uniforms(struct vsx_guide_painter *painter,
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

        gl->glUniform3f(program->color_uniform, 1.0f, 1.0f, 1.0f);
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

        if (painter->start_time == 0) {
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

        for (int i = 0; i < N_ANIMATIONS; i++) {
                const struct compiled_animation *animation =
                        painter->animations + i;

                if (elapsed_time < animation->start)
                        break;

                struct thing_pos *pos = (painter->thing_positions +
                                         animation->thing);

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

                /* Move this thing to the end so that it will be drawn on top */
                vsx_list_remove(&pos->link);
                vsx_list_insert(painter->thing_list.prev, &pos->link);

                switch (animation->click_type) {
                case ANIMATION_CLICK_TYPE_NONE:
                        break;

                case ANIMATION_CLICK_TYPE_SHORT:
                        if (t < CLICK_TIME)
                                painter->clicking = true;
                        break;

                case ANIMATION_CLICK_TYPE_DRAG:
                        painter->clicking = true;
                        break;
                }
        }
}

static void
update_tiles(struct vsx_guide_painter *painter)
{
        vsx_tile_tool_begin_update(painter->tile_buffer, EXAMPLE_WORD_LENGTH);

        struct thing_pos *pos;

        vsx_list_for_each(pos, &painter->thing_list, link) {
                if (pos->num == MOVE_CURSOR)
                        continue;

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
paint_cb(void *painter_data)
{
        struct vsx_guide_painter *painter = painter_data;

        if (!vsx_tile_tool_is_ready(painter->toolbox->tile_tool) ||
            !vsx_shadow_painter_is_ready(painter->toolbox->shadow_painter))
                return;

        update_animations(painter);
        update_tiles(painter);

        paint_shadow(painter);

        const struct vsx_shader_data *shader_data =
                &painter->toolbox->shader_data;

        const struct vsx_shader_data_program_data *program =
                shader_data->programs + VSX_SHADER_DATA_PROGRAM_SOLID;

        struct vsx_gl *gl = painter->toolbox->gl;

        gl->glUseProgram(program->program);

        set_uniforms(painter, program);

        vsx_array_object_bind(painter->vao, gl);

        gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, N_VERTICES);

        vsx_tile_tool_paint(painter->tile_buffer,
                            shader_data,
                            painter->toolbox->paint_state.pixel_matrix,
                            painter->toolbox->paint_state.pixel_translation);

        draw_cursor(painter,
                    painter->thing_positions[MOVE_CURSOR].x + painter->image_x,
                    painter->thing_positions[MOVE_CURSOR].y + painter->image_y,
                    painter->clicking);

        vsx_layout_paint_multiple(&painter->paragraph, 1);

        vsx_signal_emit(&painter->redraw_needed_signal, NULL);
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
                vsx_game_state_set_dialog(painter->game_state,
                                          VSX_DIALOG_NONE);
                return true;
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
                return false;

        case VSX_INPUT_EVENT_TYPE_CLICK:
                return handle_click(painter, event);
        }

        return false;
}

static struct vsx_signal *
get_redraw_needed_signal_cb(void *painter_data)
{
        struct vsx_guide_painter *painter = painter_data;

        return &painter->redraw_needed_signal;
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

        if (painter->paragraph.layout)
                vsx_layout_free(painter->paragraph.layout);

        if (painter->cursor_token)
                vsx_image_loader_cancel(painter->cursor_token);
        if (painter->cursor_tex)
                gl->glDeleteTextures(1, &painter->cursor_tex);

        if (painter->cursor_vbo)
                gl->glDeleteBuffers(1, &painter->cursor_vbo);
        if (painter->cursor_vao)
                vsx_array_object_free(painter->cursor_vao, gl);

        vsx_tile_tool_free_buffer(painter->tile_buffer);

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
        .get_redraw_needed_signal_cb = get_redraw_needed_signal_cb,
        .free_cb = free_cb,
};
