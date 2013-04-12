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

#ifndef __VSX_RESPONSE_H__
#define __VSX_RESPONSE_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define VSX_TYPE_RESPONSE                                               \
  (vsx_response_get_type())
#define VSX_RESPONSE(obj)                                               \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                                   \
                               VSX_TYPE_RESPONSE,                       \
                               VsxResponse))
#define VSX_RESPONSE_CLASS(klass)                                       \
  (G_TYPE_CHECK_CLASS_CAST ((klass),                                    \
                            VSX_TYPE_RESPONSE,                          \
                            VsxResponseClass))
#define VSX_IS_RESPONSE(obj)                                            \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                                   \
                               VSX_TYPE_RESPONSE))
#define VSX_IS_RESPONSE_CLASS(klass)                                    \
  (G_TYPE_CHECK_CLASS_TYPE ((klass),                                    \
                            VSX_TYPE_RESPONSE))
#define VSX_RESPONSE_GET_CLASS(obj)                                     \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                                    \
                              VSX_RESPONSE,                             \
                              VsxResponseClass))

#define VSX_RESPONSE_DISABLE_CACHE_HEADERS \
  "Cache-Control: no-cache\r\n"

#define VSX_RESPONSE_COMMON_HEADERS \
  "Server: verda-sxtelo/" PACKAGE_VERSION "\r\n" \
  "Access-Control-Allow-Origin: *\r\n"

typedef struct _VsxResponse      VsxResponse;
typedef struct _VsxResponseClass VsxResponseClass;

struct _VsxResponseClass
{
  GObjectClass parent_class;

  /* This should fill the given array with some more data to write and
     return the number of bytes added. */
  unsigned int (* add_data) (VsxResponse *response,
                             guint8 *buffer,
                             unsigned int buffer_size);
  /* This should report TRUE if there is data immediately ready for
     writing (eg, we should block for writing on the
     socket. Overriding this is optional and the default
     implementation just returns TRUE */
  gboolean (* has_data) (VsxResponse *response);
  /* This should return TRUE once the response is fully generated */
  gboolean (* is_finished) (VsxResponse *response);
};

struct _VsxResponse
{
  GObject parent;
};

GType
vsx_response_get_type (void) G_GNUC_CONST;

unsigned int
vsx_response_add_data (VsxResponse *response,
                       guint8 *buffer,
                       unsigned int buffer_size);

gboolean
vsx_response_is_finished (VsxResponse *response);

gboolean
vsx_response_has_data (VsxResponse *response);

void
vsx_response_changed (VsxResponse *response);

G_END_DECLS

#endif /* __VSX_RESPONSE_H__ */
