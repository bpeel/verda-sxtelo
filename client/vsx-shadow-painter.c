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

#include "vsx-shadow-painter.h"

#include <stdint.h>
#include <stdio.h>
#include <assert.h>

#include "vsx-map-buffer.h"
#include "vsx-mipmap.h"
#include "vsx-gl.h"
#include "vsx-array-object.h"
#include "vsx-util.h"
#include "vsx-signal.h"

struct vsx_shadow_painter {
        struct vsx_gl *gl;
        struct vsx_image_loader *image_loader;
        struct vsx_map_buffer *map_buffer;

        GLuint tex;
        struct vsx_image_loader_token *image_token;
        GLuint element_buffer;

        int shadow_width;

        struct vsx_signal ready_signal;
};

struct vsx_shadow_painter_shadow {
        struct vsx_array_object *vao;
        GLuint vbo;
};

struct vertex {
        int16_t x, y;
        uint8_t s, t;
};

/* We only need to define the vertices for the corner quads. The other
 * four quads can share the vertices of the corners.
 */
#define N_VERTICES (4 * 4)

/* We need six elements for each quad and there are 8 quads (the four
 * corners, the two horizontal bands and the two vertical bands).
 */
#define N_ELEMENTS (8 * 6)

#define ELEMENT_BUFFER_SIZE (N_ELEMENTS * sizeof (uint8_t))

/* Width in mm of the shadow */
#define SHADOW_WIDTH 4

static void
texture_load_cb(const struct vsx_image *image,
                struct vsx_error *error,
                void *data)
{
        struct vsx_shadow_painter *painter = data;

        painter->image_token = NULL;

        if (error) {
                fprintf(stderr,
                        "error loading shadow image: %s\n",
                        error->message);
                return;
        }

        struct vsx_gl *gl = painter->gl;

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

        vsx_signal_emit(&painter->ready_signal, NULL);
}

struct vsx_shadow_painter *
vsx_shadow_painter_new(struct vsx_gl *gl,
                       struct vsx_image_loader *image_loader,
                       struct vsx_map_buffer *map_buffer,
                       int dpi)
{
        struct vsx_shadow_painter *painter = vsx_calloc(sizeof *painter);

        vsx_signal_init(&painter->ready_signal);

        painter->gl = gl;
        painter->image_loader = image_loader;
        painter->map_buffer = map_buffer;

        painter->image_token = vsx_image_loader_load(image_loader,
                                                     "shadow.mpng",
                                                     texture_load_cb,
                                                     painter);

        painter->shadow_width = SHADOW_WIDTH * dpi * 10 / 254;

        return painter;
}

bool
vsx_shadow_painter_is_ready(struct vsx_shadow_painter *painter)
{
        return painter->tex != 0;
}

struct vsx_signal *
vsx_shadow_painter_get_ready_signal(struct vsx_shadow_painter *painter)
{
        return &painter->ready_signal;
}

static void
generate_elements(uint8_t *elements)
{
        uint8_t *e = elements;

#define QUAD(a, b, c, d)                        \
        do {                                    \
                *(e++) = (a);                   \
                *(e++) = (b);                   \
                *(e++) = (c);                   \
                *(e++) = (c);                   \
                *(e++) = (b);                   \
                *(e++) = (d);                   \
        } while (0)

        /* Top-left corner */
        QUAD(0, 1, 2, 3);
        /* Top horizontal band */
        QUAD(2, 3, 4, 5);
        /* Top-right corner */
        QUAD(4, 5, 6, 7);
        /* Right vertical band */
        QUAD(5, 12, 7, 14);
        /* Bottom-right corner */
        QUAD(12, 13, 14, 15);
        /* Bottom horizontal band */
        QUAD(10, 11, 12, 13);
        /* Bottom-left corner */
        QUAD(8, 9, 10, 11);
        /* Right vertical band */
        QUAD(1, 8, 3, 10);

#undef QUAD

        assert(e - elements == N_ELEMENTS);
}

static void
set_element_buffer(struct vsx_shadow_painter *painter,
                   struct vsx_array_object *vao)
{
        struct vsx_gl *gl = painter->gl;

        if (painter->element_buffer) {
                vsx_array_object_set_element_buffer(vao,
                                                    gl,
                                                    painter->element_buffer);
                return;
        }

        gl->glGenBuffers(1, &painter->element_buffer);

        vsx_array_object_set_element_buffer(vao,
                                            gl,
                                            painter->element_buffer);

        gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         ELEMENT_BUFFER_SIZE,
                         NULL, /* data */
                         GL_STATIC_DRAW);

        uint8_t *elements =
                vsx_map_buffer_map(painter->map_buffer,
                                   GL_ELEMENT_ARRAY_BUFFER,
                                   ELEMENT_BUFFER_SIZE,
                                   false, /* flush_explicit */
                                   GL_STATIC_DRAW);

        generate_elements(elements);

        vsx_map_buffer_unmap(painter->map_buffer);
}

static void
store_quad(struct vertex *v,
           int x, int y,
           int w, int h,
           int s1, int t1,
           int s2, int t2)
{
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
generate_vertices(struct vertex *vertices,
                  int w, int h,
                  int shadow_width)
{
        /* Top-left corner */
        store_quad(vertices + 0,
                   -shadow_width, -shadow_width, /* x/y */
                   shadow_width, shadow_width,
                   255, 255, /* s1, t1 */
                   0, 0 /* s2, t2 */);
        /* Top-right corner */
        store_quad(vertices + 4,
                   w, -shadow_width,
                   shadow_width, shadow_width,
                   0, 255, /* s1, t1 */
                   255, 0 /* s2, t2 */);
        /* Bottom-left corner */
        store_quad(vertices + 8,
                   -shadow_width, h,
                   shadow_width, shadow_width,
                   255, 0, /* s1, t1 */
                   0, 255 /* s2, t2 */);
        /* Bottom-right corner */
        store_quad(vertices + 12,
                   w, h,
                   shadow_width, shadow_width,
                   0, 0, /* s1, t1 */
                   255, 255 /* s2, t2 */);
}

struct vsx_shadow_painter_shadow *
vsx_shadow_painter_create_shadow(struct vsx_shadow_painter *painter,
                                 int w, int h)
{
        struct vsx_shadow_painter_shadow *shadow = vsx_calloc(sizeof *shadow);

        struct vsx_gl *gl = painter->gl;

        gl->glGenBuffers(1, &shadow->vbo);
        gl->glBindBuffer(GL_ARRAY_BUFFER, shadow->vbo);
        gl->glBufferData(GL_ARRAY_BUFFER,
                         N_VERTICES * sizeof (struct vertex),
                         NULL, /* data */
                         GL_STATIC_DRAW);

        shadow->vao = vsx_array_object_new(gl);

        vsx_array_object_set_attribute(shadow->vao,
                                       gl,
                                       VSX_SHADER_DATA_ATTRIB_POSITION,
                                       2, /* size */
                                       GL_SHORT,
                                       false, /* normalized */
                                       sizeof (struct vertex),
                                       0, /* divisor */
                                       shadow->vbo,
                                       offsetof(struct vertex, x));
        vsx_array_object_set_attribute(shadow->vao,
                                       gl,
                                       VSX_SHADER_DATA_ATTRIB_TEX_COORD,
                                       2, /* size */
                                       GL_UNSIGNED_BYTE,
                                       true, /* normalized */
                                       sizeof (struct vertex),
                                       0, /* divisor */
                                       shadow->vbo,
                                       offsetof(struct vertex, s));

        struct vertex *vertices =
                vsx_map_buffer_map(painter->map_buffer,
                                   GL_ARRAY_BUFFER,
                                   N_VERTICES * sizeof (struct vertex),
                                   false, /* flush_explicit */
                                   GL_STATIC_DRAW);

        generate_vertices(vertices, w, h, painter->shadow_width);

        vsx_map_buffer_unmap(painter->map_buffer);

        set_element_buffer(painter, shadow->vao);

        return shadow;
}

void
vsx_shadow_painter_paint(struct vsx_shadow_painter *painter,
                         struct vsx_shadow_painter_shadow *shadow,
                         const struct vsx_shader_data *shader_data,
                         const GLfloat *matrix,
                         const GLfloat *translation)
{
        if (painter->tex == 0)
                return;

        struct vsx_gl *gl = painter->gl;

        gl->glBindTexture(GL_TEXTURE_2D, painter->tex);

        const struct vsx_shader_data_program_data *program =
                shader_data->programs + VSX_SHADER_DATA_PROGRAM_TEXTURE;

        gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl->glEnable(GL_BLEND);

        gl->glUseProgram(program->program);

        gl->glUniformMatrix2fv(program->matrix_uniform,
                               1, /* count */
                               GL_FALSE, /* transpose */
                               matrix);
        gl->glUniform2f(program->translation_uniform,
                        translation[0],
                        translation[1]);

        vsx_array_object_bind(shadow->vao, gl);

        vsx_gl_draw_range_elements(gl,
                                   GL_TRIANGLES,
                                   0, N_VERTICES - 1,
                                   N_ELEMENTS,
                                   GL_UNSIGNED_BYTE,
                                   NULL /* offset */);

        gl->glDisable(GL_BLEND);
}

void
vsx_shadow_painter_free_shadow(struct vsx_shadow_painter *painter,
                               struct vsx_shadow_painter_shadow *shadow)
{
        struct vsx_gl *gl = painter->gl;

        vsx_array_object_free(shadow->vao, gl);
        gl->glDeleteBuffers(1, &shadow->vbo);

        vsx_free(shadow);
}

void
vsx_shadow_painter_free(struct vsx_shadow_painter *painter)
{
        struct vsx_gl *gl = painter->gl;

        /* This is lazily created so it might be 0 */
        if (painter->element_buffer)
                gl->glDeleteBuffers(1, &painter->element_buffer);

        if (painter->tex)
                gl->glDeleteTextures(1, &painter->tex);
        if (painter->image_token)
                vsx_image_loader_cancel(painter->image_token);

        vsx_free(painter);
}
