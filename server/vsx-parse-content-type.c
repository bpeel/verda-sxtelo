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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>

#include "vsx-parse-content-type.h"

#define HTTP_TYPE_ALPHA 1
#define HTTP_TYPE_CHAR 2
#define HTTP_TYPE_CR 4
#define HTTP_TYPE_CTL 8
#define HTTP_TYPE_DIGIT 16
#define HTTP_TYPE_HEX 32
#define HTTP_TYPE_HT 64
#define HTTP_TYPE_LF 128
#define HTTP_TYPE_LOALPHA 256
#define HTTP_TYPE_LWS 512
#define HTTP_TYPE_OCTET 1024
#define HTTP_TYPE_SEPARATOR 2048
#define HTTP_TYPE_SP 4096
#define HTTP_TYPE_TEXT 8192
#define HTTP_TYPE_TOKEN 16384
#define HTTP_TYPE_UPALPHA 32768

static const guint16
http_char_table[] =
  {
    0x040a,0x040a,0x040a,0x040a,0x040a,0x040a,0x040a,0x040a,
    0x040a,0x2e4a,0x268a,0x040a,0x040a,0x260e,0x040a,0x040a,
    0x040a,0x040a,0x040a,0x040a,0x040a,0x040a,0x040a,0x040a,
    0x040a,0x040a,0x040a,0x040a,0x040a,0x040a,0x040a,0x040a,
    0x3e02,0x6402,0x2c02,0x6402,0x6402,0x6402,0x6402,0x6402,
    0x2c02,0x2c02,0x6402,0x6402,0x2c02,0x6402,0x6402,0x2c02,
    0x6432,0x6432,0x6432,0x6432,0x6432,0x6432,0x6432,0x6432,
    0x6432,0x6432,0x2c02,0x2c02,0x2c02,0x2c02,0x2c02,0x2c02,
    0x2c02,0xe523,0xe523,0xe523,0xe523,0xe523,0xe523,0xe503,
    0xe503,0xe503,0xe503,0xe503,0xe503,0xe503,0xe503,0xe503,
    0xe503,0xe503,0xe503,0xe503,0xe503,0xe503,0xe503,0xe503,
    0xe503,0xe503,0xe503,0x2c02,0x2c02,0x2c02,0x6402,0x6402,
    0x6402,0x6422,0x6422,0x6422,0x6422,0x6422,0x6422,0x6402,
    0x6402,0x6402,0x6402,0x6402,0x6402,0x6402,0x6402,0x6402,
    0x6402,0x6402,0x6402,0x6402,0x6402,0x6402,0x6402,0x6402,
    0x6402,0x6402,0x6402,0x2c02,0x6402,0x2c02,0x6402,0x040a,
    0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,
    0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,
    0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,
    0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,
    0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,
    0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,
    0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,
    0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,
    0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,
    0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,
    0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,
    0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,
    0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,
    0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,
    0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,
    0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400,0x2400
  };

#define HTTP_IS_TOKEN(ch) (http_char_table[(guint8) ch] & HTTP_TYPE_TOKEN)
#define HTTP_IS_LWS(ch) (http_char_table[(guint8) ch] & HTTP_TYPE_LWS)
#define HTTP_IS_TEXT(ch) (http_char_table[(guint8) ch] & HTTP_TYPE_TEXT)
#define HTTP_IS_CHAR(ch) (http_char_table[(guint8) ch] & HTTP_TYPE_CHAR)

gboolean
vsx_parse_content_type (const char *header_value,
                        VsxParseContentTypeGotTypeFunc got_type_func,
                        VsxParseContentTypeGotAttributeFunc got_attribute_func,
                        gpointer user_data)
{
  GString *buf = g_string_new (NULL);
  gboolean ret = TRUE;
  const char *p = header_value, *end;
  unsigned int value_start;

  while (HTTP_IS_LWS (*p))
    p++;

  if (!HTTP_IS_TOKEN (*p))
    {
      ret = FALSE;
      goto done;
    }

  for (end = p + 1;
       HTTP_IS_TOKEN (*end);
       end++);

  g_string_append_len (buf, p, end - p);

  p = end;

  if (*p != '/')
    {
      ret = FALSE;
      goto done;
    }

  g_string_append_c (buf, '/');

  p++;

  if (!HTTP_IS_TOKEN (*p))
    {
      ret = FALSE;
      goto done;
    }

  for (end = p + 1;
       HTTP_IS_TOKEN (*end);
       end++);

  g_string_append_len (buf, p, end - p);

  p = end;

  if (!got_type_func (buf->str, user_data))
    {
      ret = FALSE;
      goto done;
    }

  while (TRUE)
    {
      while (HTTP_IS_LWS (*p))
        p++;
      if (*p == '\0')
        break;

      if (*p != ';')
        {
          ret = FALSE;
          goto done;
        }

      p++;

      while (HTTP_IS_LWS (*p))
        p++;

      if (!HTTP_IS_TOKEN (*p))
        {
          ret = FALSE;
          goto done;
        }

      for (end = p + 1;
           HTTP_IS_TOKEN (*end);
           end++);

      g_string_set_size (buf, 0);
      g_string_append_len (buf, p, end - p);
      g_string_append_c (buf, '\0');
      value_start = buf->len;

      p = end;

      if (*p != '=')
        {
          ret = FALSE;
          goto done;
        }
      p++;

      if (*p == '"')
        {
          p++;
          while (TRUE)
            if (*p == '"')
              {
                p++;
                break;
              }
            else if (*p == '\\')
              {
                if (p[1] == '\0' || !HTTP_IS_CHAR (p[1]))
                  {
                    ret = FALSE;
                    goto done;
                  }
                g_string_append_c (buf, p[1]);
                p += 2;
              }
            else if (HTTP_IS_TEXT (*p))
              {
                g_string_append_c (buf, *p);
                p++;
              }
            else
              {
                ret = FALSE;
                goto done;
              }
        }
      else if (HTTP_IS_TOKEN (*p))
        {
          for (end = p + 1;
               HTTP_IS_TOKEN (*end);
               end++);

          g_string_append_len (buf, p, end - p);

          p = end;
        }
      else
        {
          ret = FALSE;
          goto done;
        }

      if (!got_attribute_func (buf->str,
                               buf->str + value_start,
                               user_data))
        {
          ret = FALSE;
          goto done;
        }
    }

 done:
  g_string_free (buf, TRUE);

  return ret;
}
