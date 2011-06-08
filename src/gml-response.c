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
#include "gml-marshal.h"

G_DEFINE_ABSTRACT_TYPE (GmlResponse, gml_response, G_TYPE_OBJECT);

enum
{
  CHANGED_SIGNAL,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

static gboolean
gml_response_real_has_data (GmlResponse *self)
{
  return TRUE;
}

static void
gml_response_class_init (GmlResponseClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  klass->has_data = gml_response_real_has_data;

  signals[CHANGED_SIGNAL] =
    g_signal_new ("changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_FIRST,
                  0, /* no class method */
                  NULL, /* accumulator */
                  NULL, /* accu_data */
                  gml_marshal_VOID__VOID,
                  G_TYPE_NONE, /* return type */
                  0 /* num arguments */);
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

gboolean
gml_response_has_data (GmlResponse *response)
{
  return GML_RESPONSE_GET_CLASS (response)->has_data (response);
}

void
gml_response_changed (GmlResponse *response)
{
  g_signal_emit (response,
                 signals[CHANGED_SIGNAL],
                 0 /* detail */);
}
