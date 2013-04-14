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

#include <glib-object.h>

#include "vsx-response.h"

G_DEFINE_ABSTRACT_TYPE (VsxResponse, vsx_response, G_TYPE_OBJECT);

static gboolean
vsx_response_real_has_data (VsxResponse *self)
{
  return TRUE;
}

static void
vsx_response_class_init (VsxResponseClass *klass)
{
  klass->has_data = vsx_response_real_has_data;
}

static void
vsx_response_init (VsxResponse *self)
{
  vsx_signal_init (&self->changed_signal);
}

unsigned int
vsx_response_add_data (VsxResponse *response,
                       guint8 *buffer,
                       unsigned int buffer_size)
{
  return VSX_RESPONSE_GET_CLASS (response)->add_data (response,
                                                      buffer,
                                                      buffer_size);
}

gboolean
vsx_response_is_finished (VsxResponse *response)
{
  return VSX_RESPONSE_GET_CLASS (response)->is_finished (response);
}

gboolean
vsx_response_has_data (VsxResponse *response)
{
  return VSX_RESPONSE_GET_CLASS (response)->has_data (response);
}

void
vsx_response_changed (VsxResponse *response)
{
  vsx_signal_emit (&response->changed_signal, response);
}
