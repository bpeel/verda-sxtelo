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
#include "vsx-buffer.h"

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
  struct vsx_buffer buf = VSX_BUFFER_STATIC_INIT;
  unsigned long errnum = ERR_get_error ();

  vsx_buffer_append_string (&buf, "SSL error: ");
  vsx_buffer_ensure_size (&buf, buf.length + 200);
  ERR_error_string (errnum, (char *) buf.data + buf.length);

  g_set_error (error,
               VSX_SSL_ERROR,
               vsx_ssl_error_from_errno (errnum),
               "%s",
               (char *) buf.data);

  vsx_buffer_destroy (&buf);
}

GQuark
vsx_ssl_error_quark (void)
{
  return g_quark_from_static_string ("vsx-ssl-error");
}
