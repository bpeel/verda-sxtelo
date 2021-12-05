/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2011, 2013  Neil Roberts
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

#ifndef __VSX_RESPONSE_H__
#define __VSX_RESPONSE_H__

#include <glib.h>
#include <stdint.h>

#include "vsx-signal.h"
#include "vsx-object.h"

G_BEGIN_DECLS

#define VSX_RESPONSE_DISABLE_CACHE_HEADERS \
  "Cache-Control: no-cache\r\n"

#define VSX_RESPONSE_COMMON_HEADERS \
  "Server: verda-sxtelo/" PACKAGE_VERSION "\r\n" \
  "Access-Control-Allow-Origin: *\r\n"

typedef struct _VsxResponse VsxResponse;

typedef struct
{
  VsxObjectClass parent_class;

  /* This should fill the given array with some more data to write and
     return the number of bytes added. */
  unsigned int (* add_data) (VsxResponse *response,
                             uint8_t *buffer,
                             unsigned int buffer_size);
  /* This should report TRUE if there is data immediately ready for
     writing (eg, we should block for writing on the
     socket. Overriding this is optional and the default
     implementation just returns TRUE */
  gboolean (* has_data) (VsxResponse *response);
  /* This should return TRUE once the response is fully generated */
  gboolean (* is_finished) (VsxResponse *response);
}  VsxResponseClass;

struct _VsxResponse
{
  VsxObject parent;

  VsxSignal changed_signal;
};

void
vsx_response_init (void *object);

const VsxResponseClass *
vsx_response_get_class (void) G_GNUC_CONST;

unsigned int
vsx_response_add_data (VsxResponse *response,
                       uint8_t *buffer,
                       unsigned int buffer_size);

gboolean
vsx_response_is_finished (VsxResponse *response);

gboolean
vsx_response_has_data (VsxResponse *response);

void
vsx_response_changed (VsxResponse *response);

G_END_DECLS

#endif /* __VSX_RESPONSE_H__ */
