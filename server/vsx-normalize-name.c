/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2013, 2021  Neil Roberts
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

#include "vsx-normalize-name.h"
#include "vsx-proto.h"

bool
vsx_normalize_name (char *name)
{
  uint8_t *dst = (uint8_t *) name;
  const uint8_t *src = dst;
  bool got_letter = false;

  /* Skip leading whitespace */
  while (*src && g_ascii_isspace (*src))
    src++;

  /* Combine any other sequences of whitespace characters into a
   * single space */
  for (; *src; src++)
    {
      if (g_ascii_isspace (*src))
        {
          *(dst++) = ' ';
          while (src[1] && g_ascii_isspace (src[1]))
            src++;
        }
      /* Don't allow any control characters */
      else if (*src <= ' ')
        return false;
      else
        {
          *(dst++) = *src;
          got_letter = true;
        }
    }

  /* We must have at least one non-whitespace character */
  if (!got_letter)
    return false;

  /* String off any trailing space */
  if (dst[-1] == ' ')
    dst--;

  if (dst - (uint8_t *) name > VSX_PROTO_MAX_NAME_LENGTH)
    return false;

  *dst = '\0';

  return true;
}
