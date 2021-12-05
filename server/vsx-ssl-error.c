/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2021  Neil Roberts
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

#include "vsx-ssl-error.h"

#include <openssl/err.h>
#include <stdbool.h>

VsxSslError
vsx_ssl_error_from_errno (unsigned long errnum)
{
  return VSX_SSL_ERROR_OTHER;
}

void
vsx_ssl_error_set (GError **error)
{
  GString *buf = g_string_new (NULL);
  unsigned long errnum = ERR_get_error ();

  g_string_append (buf, "SSL error: ");
  size_t old_length = buf->len;
  g_string_set_size (buf, old_length + 200);
  ERR_error_string (errnum, buf->str + old_length);

  g_set_error (error,
               VSX_SSL_ERROR,
               vsx_ssl_error_from_errno (errnum),
               "%s",
               buf->str);

  g_string_free (buf, true);
}

GQuark
vsx_ssl_error_quark (void)
{
  return g_quark_from_static_string ("vsx-ssl-error");
}
