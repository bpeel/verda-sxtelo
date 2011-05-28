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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib-object.h>

#include "gml-response.h"

G_DEFINE_ABSTRACT_TYPE (GmlResponse, gml_response, G_TYPE_OBJECT);

static void
gml_response_class_init (GmlResponseClass *klass)
{
}

static void
gml_response_init (GmlResponse *self)
{
}

unsigned int
gml_response_add_data (GmlResponse *response,
                       guint8 *buffer,
                       unsigned int buffer_size)
{
  return GML_RESPONSE_GET_CLASS (response)->add_data (response,
                                                      buffer,
                                                      buffer_size);
}

gboolean
gml_response_is_finished (GmlResponse *response)
{
  return GML_RESPONSE_GET_CLASS (response)->is_finished (response);
}
