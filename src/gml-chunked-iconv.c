/*
 * Gemelo - A person for chatting with strangers in a foreign language
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <errno.h>
#include <string.h>

#include "gml-chunked-iconv.h"

void
gml_chunked_iconv_init (GmlChunkedIconv *self,
                        GIConv cd,
                        GString *output_string)
{
  self->cd = cd;
  self->output_string = output_string;
  self->mb_buf_len = 0;
  self->output_length = 0;
}

static void
prep_string (GmlChunkedIconv *self,
             unsigned int data_length)
{
  /* Make sure there's at least data_length*2 bytes or 16 (whichever's
     bigger) available in the string to store output */
  g_string_set_size (self->output_string,
                     MAX (MAX (data_length * 2 + self->output_length, 16),
                          /* Never make the string smaller */
                          self->output_string->len));
}

gboolean
gml_chunked_iconv_add_data (GmlChunkedIconv *self,
                            const guint8 *data,
                            unsigned int length)
{
  gchar *in_pos;
  gsize inbytes_left;
  gchar *out_pos, *old_out_pos;
  gsize outbytes_left;

  /* If we've got an incomplete multibyte sequence from the last chunk
     then we'll add one byte at a time from the new data until we
     complete the sequence. That way we can be sure we're at the start
     of a sequence for the rest of the chunk and we can process the
     whole batch */

  if (self->mb_buf_len > 0)
    {
      prep_string (self, length);

      while (TRUE)
        {
          if (length < 1)
            /* We need more data to complete the sequence but this isn't
               an error yet */
            return TRUE;

          /* Add one byte */
          self->mb_buf[self->mb_buf_len++] =  *(data++);
          length--;

          /* Try the conversion */
          in_pos = self->mb_buf;
          inbytes_left = self->mb_buf_len;
          out_pos = self->output_string->str + self->output_length;
          old_out_pos = out_pos;
          outbytes_left = self->output_string->len - self->output_length;
          if (g_iconv (self->cd,
                       &in_pos,
                       &inbytes_left,
                       &out_pos,
                       &outbytes_left) == -1)
            {
              if (errno == EINVAL)
                {
                  /* We still haven't got enough characters. If the
                     conversion has generated or consumed any characters
                     then something weird has happened so we'll give
                     up */
                  if (in_pos != self->mb_buf || out_pos != old_out_pos)
                    return FALSE;
                }
              else
                /* Something else has gone wrong which we can't handle */
                return FALSE;
            }
          else
            {
              /* If it hasn't consumed all of the characters then
                 something weird has happened so we'll give up */
              if (inbytes_left != 0)
                return FALSE;

              self->output_length = out_pos - self->output_string->str;
              self->mb_buf_len = 0;

              break;
            }
        }
    }

  while (length > 0)
    {
      prep_string (self, length);

      in_pos = (char *) data;
      inbytes_left = length;
      old_out_pos = out_pos = self->output_string->str + self->output_length;
      outbytes_left = self->output_string->len - self->output_length;

      /* Try a conversion */
      if (g_iconv (self->cd,
                   &in_pos,
                   &inbytes_left,
                   &out_pos,
                   &outbytes_left) == -1)
        {
          if (errno == EINVAL)
            {
              if (inbytes_left >= GML_CHUNKED_ICONV_MAX_MB_SEQUENCE
                  || inbytes_left == 0)
                return FALSE;

              /* Store the unused bytes to try again once we get more data */
              memcpy (self->mb_buf, in_pos, inbytes_left);
              self->mb_buf_len = inbytes_left;
              self->output_length = out_pos - self->output_string->str;

              break;
            }
          else if (errno == E2BIG)
            {
              /* If we didn't consume any characters then something
                 has gone seriously wrong so we'll give up */
              if (in_pos == (gchar *) data)
                return FALSE;
              /* Otherwise we'll let the loop continue and it will
                 re-prep the string */
              data = (guint8 *) in_pos;
              length = inbytes_left;
              self->output_length = out_pos - self->output_string->str;
            }
          else
            /* The conversion has properly failed */
            return FALSE;
        }
      else
        {
          /* We've successfully consumed all of the data */
          self->output_length = out_pos - self->output_string->str;
          break;
        }
    }

  return TRUE;
}

gboolean
gml_chunked_iconv_eos (GmlChunkedIconv *self)
{
  g_string_set_size (self->output_string, self->output_length);

  /* If there's a pending multi-byte sequence to complete then the
     data is invalid */

  return self->mb_buf_len == 0;
}
