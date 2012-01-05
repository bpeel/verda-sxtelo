/*
 * Gemelo - A server for chatting with strangers in a foreign language
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

#ifndef __GML_RESPONSE_H__
#define __GML_RESPONSE_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define GML_TYPE_RESPONSE                                               \
  (gml_response_get_type())
#define GML_RESPONSE(obj)                                               \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                                   \
                               GML_TYPE_RESPONSE,                       \
                               GmlResponse))
#define GML_RESPONSE_CLASS(klass)                                       \
  (G_TYPE_CHECK_CLASS_CAST ((klass),                                    \
                            GML_TYPE_RESPONSE,                          \
                            GmlResponseClass))
#define GML_IS_RESPONSE(obj)                                            \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                                   \
                               GML_TYPE_RESPONSE))
#define GML_IS_RESPONSE_CLASS(klass)                                    \
  (G_TYPE_CHECK_CLASS_TYPE ((klass),                                    \
                            GML_TYPE_RESPONSE))
#define GML_RESPONSE_GET_CLASS(obj)                                     \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                                    \
                              GML_RESPONSE,                             \
                              GmlResponseClass))

#define GML_RESPONSE_DISABLE_CACHE_HEADERS \
  "Cache-Control: no-cache\r\n"

#define GML_RESPONSE_COMMON_HEADERS \
  "Server: gemelo/" PACKAGE_VERSION "\r\n" \
  "Access-Control-Allow-Origin: *\r\n"

typedef struct _GmlResponse      GmlResponse;
typedef struct _GmlResponseClass GmlResponseClass;

struct _GmlResponseClass
{
  GObjectClass parent_class;

  /* This should fill the given array with some more data to write and
     return the number of bytes added. */
  unsigned int (* add_data) (GmlResponse *response,
                             guint8 *buffer,
                             unsigned int buffer_size);
  /* This should report TRUE if there is data immediately ready for
     writing (eg, we should block for writing on the
     socket. Overriding this is optional and the default
     implementation just returns TRUE */
  gboolean (* has_data) (GmlResponse *response);
  /* This should return TRUE once the response is fully generated */
  gboolean (* is_finished) (GmlResponse *response);
};

struct _GmlResponse
{
  GObject parent;
};

GType
gml_response_get_type (void) G_GNUC_CONST;

unsigned int
gml_response_add_data (GmlResponse *response,
                       guint8 *buffer,
                       unsigned int buffer_size);

gboolean
gml_response_is_finished (GmlResponse *response);

gboolean
gml_response_has_data (GmlResponse *response);

void
gml_response_changed (GmlResponse *response);

G_END_DECLS

#endif /* __GML_RESPONSE_H__ */
