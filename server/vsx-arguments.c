/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2013  Neil Roberts
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
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#include "vsx-arguments.h"
#include "vsx-person.h"
#include "vsx-normalize-name.h"

static gboolean
uri_decode (const char *str,
            int len,
            GString *buf)
{
  const char *s;

  g_string_set_size (buf, 0);

  for (s = str; s - str < len; s++)
    {
      if (*s == '+')
        g_string_append_c (buf, ' ');
      else if (*s == '%')
        {
          int nibble1, nibble2, value;

          if (str + len - s < 3)
            return FALSE;

          nibble1 = g_ascii_xdigit_value (s[1]);
          if (nibble1 == -1)
            return FALSE;
          nibble2 = g_ascii_xdigit_value (s[2]);
          if (nibble2 == -1)
            return FALSE;

          value = (nibble1 << 4) | nibble2;

          g_string_append_c (buf, value);

          s += 2;
        }
      else
        g_string_append_c (buf, *s);
    }

  /* This should also detect embedded NULLs */
  return g_utf8_validate (buf->str, buf->len, NULL /* end */);
}

static gboolean
parse_int (const char *str,
           int *value)
{
  int64_t v;
  char *endptr;

  errno = 0;

  v = g_ascii_strtoll (str, &endptr, 10);

  if (errno || endptr == str || *endptr || v > G_MAXINT || v < G_MININT)
    return FALSE;

  *value = (int) v;

  return TRUE;
}

gboolean
vsx_arguments_parse (const char *template,
                     const char *arg_str,
                     ...)
{
  GString *buf;
  const char *arg, *p, *last_arg;
  va_list ap, ap_copy;
  gboolean ret = TRUE;

  if (arg_str == NULL)
    return FALSE;

  buf = g_string_new (NULL);

  va_start (ap, arg_str);
  G_VA_COPY (ap_copy, ap);

  for (p = arg_str, arg = template; *arg; arg++)
    {
      const char *end = strchr (p, '&');

      if (arg[1])
        {
          /* This isn't the last argument so there should be an
           * ampersand terminator */
          if (end == NULL)
            goto arg_error;
        }
      else if (end)
        /* This is the last argument so there shouldn't be a
         * terminator */
        goto arg_error;
      else
        end = p + strlen (p);

      if (!uri_decode (p, end - p, buf))
        goto arg_error;

      switch (*arg)
        {
        case 'i': /* integer */
          {
            int *v = va_arg (ap, int *);
            if (!parse_int (buf->str, v))
              goto arg_error;
          }
          break;

        case 'p': /* person id */
          {
            VsxPersonId *v = va_arg (ap, VsxPersonId *);
            if (!vsx_person_parse_id (buf->str, v))
              goto arg_error;
          }
          break;

        case 'n': /* name */
          if (!vsx_normalize_name (buf->str))
            goto arg_error;
          /* flow through */
        case 's': /* string */
          {
            char **v = va_arg (ap, char **);
            *v = g_strdup (buf->str);
          }
          break;

        default:
          g_assert_not_reached ();
        }

      p = end + 1;
    }

  goto cleanup;

 arg_error:
  /* Cleanup all of the string arguments */
  last_arg = arg;

  for (arg = template; arg < last_arg; arg++)
    {
      switch (*arg)
        {
        case 'i':
          va_arg (ap_copy, int *);
          break;

        case 'p':
          va_arg (ap, VsxPersonId *);
          break;

        case 'n':
        case 's':
          {
            char **v = va_arg (ap_copy, char **);
            g_free (*v);
          }
          break;

        default:
          g_assert_not_reached ();
        }
    }

  ret = FALSE;

 cleanup:
  va_end (ap);
  va_end (ap_copy);
  g_string_free (buf, TRUE);

  return ret;
}
