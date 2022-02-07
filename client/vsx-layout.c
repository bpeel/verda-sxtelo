/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2021, 2022  Neil Roberts
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

#include "vsx-layout.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

#include "vsx-gl.h"
#include "vsx-util.h"
#include "vsx-utf8.h"
#include "vsx-array-object.h"
#include "vsx-shader-data.h"
#include "vsx-map-buffer.h"
#include "vsx-buffer.h"

struct vsx_layout {
        struct vsx_toolbox *toolbox;
        struct vsx_font *font;
        struct vsx_array_object *vao;
        GLuint vbo;
        struct vsx_quad_tool_buffer *quad_buffer;
        char *text;
        bool dirty;
        size_t buffer_size;
        struct vsx_buffer draw_calls;
        unsigned width;

        struct vsx_layout_extents logical_extents;
};

struct vertex {
        int16_t x, y;
        uint16_t s, t;
};

struct glyph_quad {
        int16_t x, y;
        unsigned glyph_index;
        unsigned tex_num;
};

struct draw_call {
        unsigned tex_num;
        unsigned n_elements;
        size_t offset;
};

#define VSX_LAYOUT_MINIMUM_BUFFER_SIZE 1024

struct vsx_layout *
vsx_layout_new(struct vsx_toolbox *toolbox)
{
        struct vsx_layout *layout = vsx_calloc(sizeof *layout);

        layout->toolbox = toolbox;
        layout->font = vsx_font_library_get_font(toolbox->font_library, 0);
        vsx_buffer_init(&layout->draw_calls);
        layout->width = UINT_MAX;

        return layout;
}


void
vsx_layout_set_text(struct vsx_layout *layout,
                    const char *text)
{
        vsx_free(layout->text);
        layout->text = vsx_strdup(text);
        layout->dirty = true;
}

void
vsx_layout_set_font(struct vsx_layout *layout,
                    enum vsx_font_type font)
{
        struct vsx_font_library *library = layout->toolbox->font_library;

        layout->font = vsx_font_library_get_font(library, font);
        layout->dirty = true;
}

void
vsx_layout_set_width(struct vsx_layout *layout,
                     unsigned width)
{
        /* Store the width as font units for easy comparison with the
         * x_advance
         */
        width *= 64;

        if (width == layout->width)
                return;

        layout->width = width;
        layout->dirty = true;
}

static int
compare_glyph_quad_cb(const void *a, const void *b)
{
        const struct glyph_quad *qa = a;
        const struct glyph_quad *qb = b;

        if (qa->tex_num != qb->tex_num)
                return qa->tex_num < qb->tex_num ? -1 : 1;

        return qa->x - qb->x;
}

static void
add_glyph_quads_for_line(struct vsx_layout *layout,
                         const struct vsx_font_metrics *metrics,
                         struct vsx_buffer *buf,
                         const char *line,
                         const char *end,
                         int y)
{
        int x = 0;

        for (const char *p = line; p < end; p = vsx_utf8_next(p)) {
                uint32_t ch = vsx_utf8_get_char(p);
                unsigned glyph_index = vsx_font_look_up_glyph(layout->font, ch);
                struct vsx_glyph_hash_entry *glyph =
                        vsx_font_prepare_glyph(layout->font, glyph_index);

                /* Ignore empty glyphs */
                if (glyph->tex_num != 0) {
                        vsx_buffer_set_length(buf,
                                              buf->length +
                                              sizeof (struct glyph_quad));

                        struct glyph_quad *quad =
                                ((struct glyph_quad *)
                                 (buf->data + buf->length)) - 1;

                        quad->x = (x + 32) / 64 + glyph->left;
                        quad->y = (y + 32) / 64 - glyph->top;
                        quad->glyph_index = glyph_index;
                        quad->tex_num = glyph->tex_num;
                }

                x += glyph->x_advance;
        }

        if (x > 0) {
                float right = x / 64.0f;

                if (right > layout->logical_extents.right)
                        layout->logical_extents.right = right;

                int bottom = y / 64.0f - metrics->descender;

                if (bottom > layout->logical_extents.bottom)
                        layout->logical_extents.bottom = bottom;
        }
}

static int
get_character_advance(struct vsx_layout *layout, uint32_t ch)
{
        unsigned glyph_index = vsx_font_look_up_glyph(layout->font, ch);
        struct vsx_glyph_hash_entry *glyph =
                vsx_font_prepare_glyph(layout->font, glyph_index);
        return glyph->x_advance;
}

static void
split_lines(struct vsx_layout *layout,
            const struct vsx_font_metrics *metrics,
            struct vsx_buffer *buf)
{
        int space_advance = get_character_advance(layout, ' ');
        const char *line_start = layout->text, *p = layout->text;
        int line_length = 0;
        int line_num = 0;

        int y_advance = metrics->height * 64 + 0.5f;

        while (*p) {
                int before_word = 0;

                while (*p == ' ') {
                        before_word += space_advance;
                        p++;
                }

                int word_length = 0;
                const char *word_start = p;

                while (*p != ' ' && *p != '\n' && *p) {
                        uint32_t ch = vsx_utf8_get_char(p);
                        word_length += get_character_advance(layout, ch);
                        p = vsx_utf8_next(p);
                }

                int add_length = word_length;

                if (line_length > 0)
                        add_length += before_word;
                else
                        line_start = word_start;

                if (line_length + add_length > layout->width) {
                        /* If the word on its own is too long for the
                         * line then just add it anyway.
                         */
                        if (line_length == 0) {
                                add_glyph_quads_for_line(layout,
                                                         metrics,
                                                         buf,
                                                         word_start,
                                                         p,
                                                         line_num * y_advance);
                        } else {
                                add_glyph_quads_for_line(layout,
                                                         metrics,
                                                         buf,
                                                         line_start,
                                                         word_start,
                                                         line_num * y_advance);
                                /* Try adding the word again on a new line */
                                p = word_start;
                        }

                        line_num++;
                        line_length = 0;
                        line_start = p;
                } else {
                        line_length += add_length;
                }

                if (*p == '\n') {
                        add_glyph_quads_for_line(layout,
                                                 metrics,
                                                 buf,
                                                 line_start,
                                                 p,
                                                 line_num * y_advance);
                        line_num++;
                        line_length = 0;
                        line_start = ++p;
                }
        }

        /* Add the last line. It should fit. */
        if (p > line_start) {
                add_glyph_quads_for_line(layout,
                                         metrics,
                                         buf,
                                         line_start, p,
                                         line_num * y_advance);
                line_num++;
        }

        layout->logical_extents.n_lines = line_num;
}

static void
get_glyph_quads(struct vsx_layout *layout,
                struct glyph_quad **quads_out,
                size_t *n_quads_out)
{
        memset(&layout->logical_extents, 0, sizeof layout->logical_extents);

        if (layout->text == NULL) {
                *quads_out = NULL;
                *n_quads_out = 0;
                return;
        }

        struct vsx_font_metrics metrics;
        vsx_font_get_metrics(layout->font, &metrics);

        struct vsx_buffer buf = VSX_BUFFER_STATIC_INIT;

        if (layout->width >= UINT_MAX) {
                add_glyph_quads_for_line(layout,
                                         &metrics,
                                         &buf,
                                         layout->text,
                                         layout->text + strlen(layout->text),
                                         0 /* y */);
                layout->logical_extents.n_lines = 1;
        } else {
                split_lines(layout, &metrics, &buf);
        }

        size_t n_quads = buf.length / sizeof (struct glyph_quad);

        if (n_quads > 0)
                layout->logical_extents.top = metrics.ascender;

        /* Sort the quads by the texture number so that we can group
         * draw calls by it.
         */
        qsort(buf.data,
              n_quads,
              sizeof (struct glyph_quad),
              compare_glyph_quad_cb);

        *quads_out = (void *) buf.data;
        *n_quads_out = n_quads;
}

static void
free_buffer(struct vsx_layout *layout)
{
        struct vsx_gl *gl = layout->toolbox->gl;

        if (layout->vao) {
                vsx_array_object_free(layout->vao, gl);
                layout->vao = NULL;
        }

        if (layout->vbo) {
                gl->glDeleteBuffers(1, &layout->vbo);
                layout->vbo = 0;
        }

        if (layout->quad_buffer)
                vsx_quad_tool_unref_buffer(layout->quad_buffer, gl);
}

static void
ensure_buffer_size(struct vsx_layout *layout,
                   size_t buffer_size)
{
        if (buffer_size <= layout->buffer_size)
                return;

        free_buffer(layout);

        size_t alloc_size = MAX(VSX_LAYOUT_MINIMUM_BUFFER_SIZE,
                                layout->buffer_size);

        while (alloc_size < buffer_size)
                alloc_size *= 2;

        layout->buffer_size = alloc_size;

        struct vsx_gl *gl = layout->toolbox->gl;

        gl->glGenBuffers(1, &layout->vbo);
        gl->glBindBuffer(GL_ARRAY_BUFFER, layout->vbo);
        gl->glBufferData(GL_ARRAY_BUFFER,
                         alloc_size,
                         NULL,
                         GL_DYNAMIC_DRAW);

        layout->vao = vsx_array_object_new(gl);
        vsx_array_object_set_attribute(layout->vao,
                                       gl,
                                       VSX_SHADER_DATA_ATTRIB_POSITION,
                                       2, /* size */
                                       GL_SHORT,
                                       GL_FALSE, /* normalized */
                                       sizeof (struct vertex),
                                       0, /* divisor */
                                       layout->vbo,
                                       offsetof(struct vertex, x));
        vsx_array_object_set_attribute(layout->vao,
                                       gl,
                                       VSX_SHADER_DATA_ATTRIB_TEX_COORD,
                                       2, /* size */
                                       GL_UNSIGNED_SHORT,
                                       GL_TRUE, /* normalized */
                                       sizeof (struct vertex),
                                       0, /* divisor */
                                       layout->vbo,
                                       offsetof(struct vertex, s));

        layout->quad_buffer =
                vsx_quad_tool_get_buffer(layout->toolbox->quad_tool,
                                         layout->vao,
                                         alloc_size /
                                         sizeof (struct vertex) /
                                         4);
}

static void
generate_vertices(struct vsx_layout *layout,
                  const struct glyph_quad *quads,
                  size_t n_quads,
                  struct vertex *vertices,
                  size_t buffer_size)
{
        struct vertex *v = vertices;

        for (unsigned q = 0; q < n_quads; q++) {
                struct vsx_glyph_hash_entry *glyph =
                        vsx_font_prepare_glyph(layout->font,
                                               quads[q].glyph_index);

                v->x = quads[q].x;
                v->y = quads[q].y;
                v->s = glyph->s1;
                v->t = glyph->t1;
                v++;
                v->x = quads[q].x;
                v->y = quads[q].y + glyph->height;
                v->s = glyph->s1;
                v->t = glyph->t2;
                v++;
                v->x = quads[q].x + glyph->width;
                v->y = quads[q].y;
                v->s = glyph->s2;
                v->t = glyph->t1;
                v++;
                v->x = quads[q].x + glyph->width;
                v->y = quads[q].y + glyph->height;
                v->s = glyph->s2;
                v->t = glyph->t2;
                v++;
        }

        assert((v - vertices) * sizeof (struct vertex) == buffer_size);
}

static void
generate_draw_calls(struct vsx_layout *layout,
                    const struct glyph_quad *quads,
                    size_t n_quads)
{
        vsx_buffer_set_length(&layout->draw_calls, 0);

        struct draw_call *draw_call = NULL;
        unsigned last_tex_num = -1;
        size_t offset = 0;

        for (unsigned i = 0; i < n_quads; i++) {
                if (last_tex_num != quads[i].tex_num) {
                        vsx_buffer_set_length(&layout->draw_calls,
                                              layout->draw_calls.length +
                                              sizeof (struct draw_call));

                        draw_call = ((struct draw_call *)
                                     (layout->draw_calls.data +
                                      layout->draw_calls.length) - 1);

                        draw_call->tex_num = quads[i].tex_num;
                        draw_call->n_elements = 0;
                        draw_call->offset = offset;
                        last_tex_num = draw_call->tex_num;
                }

                draw_call->n_elements += 6;
                offset += 6 * sizeof (uint16_t);
        }
}

void
vsx_layout_prepare(struct vsx_layout *layout)
{
        if (!layout->dirty)
                return;

        layout->dirty = false;

        struct glyph_quad *quads;
        size_t n_quads;

        get_glyph_quads(layout, &quads, &n_quads);

        if (n_quads == 0) {
                vsx_buffer_set_length(&layout->draw_calls, 0);
                return;
        }

        size_t buffer_size = 4 * n_quads * sizeof(struct vertex);

        ensure_buffer_size(layout, buffer_size);

        struct vsx_gl *gl = layout->toolbox->gl;

        gl->glBindBuffer(GL_ARRAY_BUFFER, layout->vbo);

        struct vertex *vertices =
                vsx_map_buffer_map(layout->toolbox->map_buffer,
                                   GL_ARRAY_BUFFER,
                                   layout->buffer_size,
                                   true, /* flush_explicit */
                                   GL_DYNAMIC_DRAW);

        generate_vertices(layout, quads, n_quads, vertices, buffer_size);

        vsx_map_buffer_flush(layout->toolbox->map_buffer, 0, buffer_size);

        vsx_map_buffer_unmap(layout->toolbox->map_buffer);

        generate_draw_calls(layout, quads, n_quads);

        vsx_free(quads);
}

const struct vsx_layout_extents *
vsx_layout_get_logical_extents(struct vsx_layout *layout)
{
        assert(!layout->dirty);

        return &layout->logical_extents;
}

static void
set_translation_uniform(struct vsx_gl *gl,
                        const struct vsx_shader_data_program_data *program,
                        const struct vsx_layout_paint_params *params,
                        int x, int y)
{
        float tx = (params->matrix[0] * x +
                    params->matrix[2] * y +
                    params->translation_x);
        float ty = (params->matrix[1] * x +
                    params->matrix[3] * y +
                    params->translation_y);

        gl->glUniform2f(program->translation_uniform, tx, ty);
}

static void
submit_layout(struct vsx_layout *layout)
{
        const struct draw_call *draw_calls =
                (const struct draw_call *) layout->draw_calls.data;
        size_t n_draw_calls = layout->draw_calls.length / sizeof draw_calls[0];
        GLuint start_index = 0;

        struct vsx_gl *gl = layout->toolbox->gl;

        for (unsigned i = 0; i < n_draw_calls; i++) {
                gl->glBindTexture(GL_TEXTURE_2D, draw_calls[i].tex_num);

                int n_verts = draw_calls[i].n_elements * 4 / 6;

                vsx_gl_draw_range_elements(gl,
                                           GL_TRIANGLES,
                                           start_index,
                                           start_index + n_verts - 1,
                                           draw_calls[i].n_elements,
                                           layout->quad_buffer->type,
                                           (void *) (intptr_t)
                                           draw_calls[i].offset);

                start_index += n_verts;
        }
}

void
vsx_layout_paint_params(const struct vsx_layout_paint_params *params)
{
        if (params->n_layouts <= 0)
                return;

        struct vsx_toolbox *toolbox = params->layouts[0].layout->toolbox;

        const struct vsx_shader_data_program_data *program =
                toolbox->shader_data.programs + VSX_SHADER_DATA_PROGRAM_LAYOUT;

        struct vsx_gl *gl = toolbox->gl;

        gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl->glEnable(GL_BLEND);

        gl->glUseProgram(program->program);

        gl->glUniformMatrix2fv(program->matrix_uniform,
                               1, /* count */
                               GL_FALSE, /* transpose */
                               params->matrix);

        for (unsigned i = 0; i < params->n_layouts; i++) {
                const struct vsx_layout_paint_position *pos =
                        params->layouts + i;

                /* All the layouts of the scene should be prepared before any
                 * of them are painted.
                 */
                assert(!pos->layout->dirty);

                if (pos->layout->draw_calls.length <= 0)
                        continue;

                vsx_array_object_bind(pos->layout->vao, gl);

                gl->glUniform3f(program->color_uniform,
                                pos->r,
                                pos->g,
                                pos->b);

                set_translation_uniform(gl,
                                        program,
                                        params,
                                        pos->x, pos->y);

                submit_layout(pos->layout);
        }

        gl->glDisable(GL_BLEND);
}

void
vsx_layout_paint_multiple(const struct vsx_layout_paint_position *layouts,
                          size_t n_layouts)
{
        if (n_layouts <= 0)
                return;

        struct vsx_paint_state *paint_state =
                &layouts[0].layout->toolbox->paint_state;

        vsx_paint_state_ensure_layout(paint_state);

        struct vsx_layout_paint_params params = {
                .layouts = layouts,
                .n_layouts = n_layouts,
                .matrix = paint_state->pixel_matrix,
                .translation_x = paint_state->pixel_translation[0],
                .translation_y = paint_state->pixel_translation[1],
        };

        vsx_layout_paint_params(&params);
}

void
vsx_layout_paint(struct vsx_layout *layout,
                 int x, int y,
                 float r, float g, float b)
{
        if (layout->draw_calls.length <= 0)
                return;

        struct vsx_paint_state *paint_state =
                &layout->toolbox->paint_state;

        vsx_paint_state_ensure_layout(paint_state);

        struct vsx_layout_paint_position pos = {
                .layout = layout,
                .x = x,
                .y = y,
                .r = r,
                .g = g,
                .b = b,
        };

        struct vsx_layout_paint_params params = {
                .layouts = &pos,
                .n_layouts = 1,
                .matrix = paint_state->pixel_matrix,
                .translation_x = paint_state->pixel_translation[0],
                .translation_y = paint_state->pixel_translation[1],
        };

        vsx_layout_paint_params(&params);
}

void
vsx_layout_free(struct vsx_layout *layout)
{
        free_buffer(layout);
        vsx_buffer_destroy(&layout->draw_calls);
        vsx_free(layout->text);
        vsx_free(layout);
}
