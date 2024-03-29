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

#include "vsx-font.h"

#include <freetype/freetype.h>
#include <freetype/ftbitmap.h>
#include <stdbool.h>
#include <assert.h>

#include "vsx-util.h"
#include "vsx-gl.h"
#include "vsx-bsp.h"

struct vsx_error_domain
vsx_font_error;

struct vsx_font_texture {
        GLuint tex;
        struct vsx_bsp *bsp;
        struct vsx_font_texture *next;
};

struct vsx_font {
        struct vsx_font_library *library;
        FT_Face face;
        struct vsx_glyph_hash *glyph_hash;
        uint8_t *font_data;
};

struct vsx_font_library {
        struct vsx_gl *gl;
        FT_Library library;
        /* Temporary bitmap for converting to grayscale */
        FT_Bitmap temp_bitmap;
        struct vsx_font_texture *textures;
        struct vsx_font *fonts[VSX_FONT_N_TYPES];
};

struct vsx_font_data {
        const char *filename;
        int face_index;
        int size;
};

static const struct vsx_font_data
font_types[] = {
        [VSX_FONT_TYPE_LABEL] = {
                .filename = "NotoSans-Regular.ttf",
                .face_index = 0,
                .size = 8 * 64,
        },
        [VSX_FONT_TYPE_SYMBOL] = {
                .filename = "symbols.otf",
                .face_index = 0,
                .size = 16 * 64,
        },
};

_Static_assert(VSX_N_ELEMENTS(font_types) == VSX_FONT_N_TYPES,
               "Missing font data");

#define VSX_FONT_TEXTURE_SIZE 1024
#define texel_to_coordinate(texel) (((texel) * UINT16_MAX + \
                                     VSX_FONT_TEXTURE_SIZE / 2) / \
                                    VSX_FONT_TEXTURE_SIZE)

static bool
load_font_data(struct vsx_asset_manager *asset_manager,
               const char *name,
               uint8_t **data_out,
               size_t *size_out,
               struct vsx_error **error)
{
        struct vsx_asset *asset =
                vsx_asset_manager_open(asset_manager, name, error);

        if (asset == NULL)
                return false;

        bool ret = true;
        size_t size;

        if (!vsx_asset_remaining(asset, &size, error)) {
                ret = false;
                goto out;
        }

        uint8_t *data = vsx_alloc(size);

        if (!vsx_asset_read(asset, data, size, error)) {
                vsx_free(data);
                ret = false;
                goto out;
        }

        *data_out = data;
        *size_out = size;

out:
        vsx_asset_close(asset);
        return ret;
}

static struct vsx_font *
open_font(struct vsx_font_library *library,
          struct vsx_asset_manager *asset_manager,
          int dpi,
          const struct vsx_font_data *font_type_data,
          struct vsx_error **error)
{
        uint8_t *font_data;
        size_t font_data_size;
        FT_Face face;

        if (!load_font_data(asset_manager,
                            font_type_data->filename,
                            &font_data,
                            &font_data_size,
                            error))
                return false;

        FT_Error ft_error = FT_New_Memory_Face(library->library,
                                               font_data,
                                               font_data_size,
                                               font_type_data->face_index,
                                               &face);

        if (ft_error) {
                vsx_free(font_data);
                vsx_set_error(error,
                              &vsx_font_error,
                              VSX_FONT_ERROR_INVALID,
                              "%s: Error loading font",
                              font_type_data->filename);
                return false;
        }

        FT_Set_Char_Size(face,
                         0, /* width (= height) */
                         font_type_data->size, /* height */
                         dpi, /* horizontal resolution */
                         dpi /* vertical resolution */);

        struct vsx_font *font = vsx_alloc(sizeof *font);

        font->face = face;
        font->glyph_hash = vsx_glyph_hash_new();
        font->library = library;
        font->font_data = font_data;

        return font;
}

static bool
open_fonts(struct vsx_font_library *library,
           struct vsx_asset_manager *asset_manager,
           int dpi,
           struct vsx_error **error)
{
        for (int i = 0; i < VSX_FONT_N_TYPES; i++) {
                library->fonts[i] = open_font(library,
                                              asset_manager,
                                              dpi,
                                              font_types + i,
                                              error);

                if (library->fonts[i] == NULL)
                        return false;
        }

        return true;
}

struct vsx_font_library *
vsx_font_library_new(struct vsx_gl *gl,
                     struct vsx_asset_manager *asset_manager,
                     int dpi,
                     struct vsx_error **error)
{
        FT_Library ft_library;

        FT_Error ft_error = FT_Init_FreeType(&ft_library);

        if (ft_error != 0) {
                vsx_set_error(error,
                              &vsx_font_error,
                              VSX_FONT_ERROR_LIBRARY,
                              "Failed to initialise Freetype");
                return NULL;
        }

        struct vsx_font_library *library = vsx_calloc(sizeof *library);

        library->gl = gl;
        library->library = ft_library;
        library->textures = NULL;
        FT_Bitmap_Init(&library->temp_bitmap);

        if (!open_fonts(library, asset_manager, dpi, error)) {
                vsx_font_library_free(library);
                return NULL;
        }

        return library;
}

static void
reserve_texture_space(struct vsx_font *font,
                      struct vsx_glyph_hash_entry *hash_entry,
                      int width, int height,
                      int *x_out, int *y_out)
{
        struct vsx_font_texture *texture;
        int x = 0, y = 0;

        for (texture = font->library->textures;
             texture;
             texture = texture->next) {
                if (vsx_bsp_add(texture->bsp, width, height, &x, &y)) {
                        goto found;
                }
        }

        texture = vsx_alloc(sizeof *texture);
        texture->next = font->library->textures;
        font->library->textures = texture;

        struct vsx_gl *gl = font->library->gl;

        texture->bsp = vsx_bsp_new(VSX_FONT_TEXTURE_SIZE,
                                   VSX_FONT_TEXTURE_SIZE);
        gl->glGenTextures(1, &texture->tex);
        gl->glBindTexture(GL_TEXTURE_2D, texture->tex);
        gl->glTexImage2D(GL_TEXTURE_2D,
                         0, /* level */
                         GL_ALPHA, /* internal_format */
                         VSX_FONT_TEXTURE_SIZE,
                         VSX_FONT_TEXTURE_SIZE,
                         0, /* border */
                         GL_ALPHA, /* format */
                         GL_UNSIGNED_BYTE,
                         NULL /* data */);
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
                            GL_LINEAR);

        vsx_bsp_add(texture->bsp, width, height, &x, &y);

found:
        hash_entry->tex_num = texture->tex;
        hash_entry->s1 = texel_to_coordinate(x);
        hash_entry->t1 = texel_to_coordinate(y);
        hash_entry->s2 = texel_to_coordinate(x + width);
        hash_entry->t2 = texel_to_coordinate(y + height);

        *x_out = x;
        *y_out = y;
}

unsigned
vsx_font_look_up_glyph(struct vsx_font *font,
                       uint32_t unicode)
{
        return FT_Get_Char_Index(font->face, unicode);
}

struct vsx_glyph_hash_entry *
vsx_font_prepare_glyph(struct vsx_font *font,
                       unsigned glyph_index)
{
        bool added;
        struct vsx_glyph_hash_entry *hash_entry =
                vsx_glyph_hash_get(font->glyph_hash, glyph_index, &added);

        if (!added)
                return hash_entry;

        hash_entry->x_advance = 0;
        hash_entry->tex_num = 0;

        FT_Error error = FT_Load_Glyph(font->face,
                                       glyph_index,
                                       FT_LOAD_RENDER);

        if (error != 0)
                return hash_entry;

        FT_GlyphSlot glyph = font->face->glyph;

        hash_entry->x_advance = glyph->advance.x;

        if (FT_Bitmap_Convert(font->library->library,
                              &glyph->bitmap,
                              &font->library->temp_bitmap,
                              4 /* alignment */) != 0)
                return hash_entry;

        hash_entry->width = font->library->temp_bitmap.width;
        hash_entry->height = font->library->temp_bitmap.rows;

        struct vsx_gl *gl = font->library->gl;

        if (hash_entry->width > 0 && hash_entry->height > 0) {
                int tex_x, tex_y;

                reserve_texture_space(font,
                                      hash_entry,
                                      hash_entry->width,
                                      hash_entry->height,
                                      &tex_x, &tex_y);

                hash_entry->left = glyph->bitmap_left;
                hash_entry->top = glyph->bitmap_top;

                gl->glBindTexture(GL_TEXTURE_2D, hash_entry->tex_num);
                gl->glTexSubImage2D(GL_TEXTURE_2D,
                                    0, /* level */
                                    tex_x, tex_y,
                                    hash_entry->width,
                                    hash_entry->height,
                                    GL_ALPHA,
                                    GL_UNSIGNED_BYTE,
                                    font->library->temp_bitmap.buffer);
        }

        return hash_entry;
}

void
vsx_font_get_metrics(struct vsx_font *font,
                     struct vsx_font_metrics *metrics)
{
        const FT_Size_Metrics *face_metrics = &font->face->size->metrics;

        metrics->ascender = face_metrics->ascender / 64.0f;
        metrics->descender = face_metrics->descender / 64.0f;
        metrics->height = face_metrics->height / 64.0f;
}

static void
free_font(struct vsx_font *font)
{
        vsx_glyph_hash_free(font->glyph_hash);

        vsx_free(font->font_data);

        vsx_free(font);
}

struct vsx_font *
vsx_font_library_get_font(struct vsx_font_library *library,
                          enum vsx_font_type type)
{
        assert(type >= 0 && type < VSX_FONT_N_TYPES);

        return library->fonts[type];
}

void
vsx_font_library_free(struct vsx_font_library *library)
{
        struct vsx_font_texture *next;

        for (int i = 0; i < VSX_N_ELEMENTS(library->fonts); i++) {
                if (library->fonts[i])
                        free_font(library->fonts[i]);
        }

        struct vsx_gl *gl = library->gl;

        for (struct vsx_font_texture *tex = library->textures;
             tex;
             tex = next) {
                next = tex->next;
                gl->glDeleteTextures(1, &tex->tex);
                vsx_bsp_free(tex->bsp);
                vsx_free(tex);
        }

        FT_Bitmap_Done(library->library, &library->temp_bitmap);
        FT_Done_FreeType(library->library);
        vsx_free(library);
}
