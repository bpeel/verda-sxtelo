/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2011  Neil Roberts
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

#ifndef __VSX_CHUNKED_ICONV_H__
#define __VSX_CHUNKED_ICONV_H__

#include <glib.h>

G_BEGIN_DECLS

/* The chunked converter assumes that no multibyte sequence needs more
   bytes than this. If it does then the conversion will fail if we
   split in the middle of a multibyte sequence */
#define VSX_CHUNKED_ICONV_MAX_MB_SEQUENCE 8

typedef struct
{
  GIConv cd;
  GString *output_string;
  char mb_buf[VSX_CHUNKED_ICONV_MAX_MB_SEQUENCE];
  unsigned int mb_buf_len;
  /* This is the length of the completed bytes written to
     output_str. We can't just use output_str->len because we need to
     grow the string to reserve space to write characters to */
  unsigned int output_length;
} VsxChunkedIconv;

void
vsx_chunked_iconv_init (VsxChunkedIconv *self,
                        GIConv cd,
                        GString *output_string);

gboolean
vsx_chunked_iconv_add_data (VsxChunkedIconv *self,
                            const guint8 *data,
                            unsigned int length);

gboolean
vsx_chunked_iconv_eos (VsxChunkedIconv *self);

G_END_DECLS

#endif /* __VSX_CHUNKED_ICONV_H__ */
