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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>

#include "vsx-response.h"

static gboolean
vsx_response_real_has_data (VsxResponse *self)
{
  return TRUE;
}

const VsxResponseClass *
vsx_response_get_class (void)
{
  static VsxResponseClass class;

  if (class.parent_class.free == NULL)
    {
      class.parent_class = *vsx_object_get_class ();
      class.parent_class.instance_size = sizeof (VsxResponse);

      class.has_data = vsx_response_real_has_data;
    }

  return &class;
}

void
vsx_response_init (void *object)
{
  VsxResponse *response = object;

  vsx_object_init (object);

  vsx_signal_init (&response->changed_signal);
}

unsigned int
vsx_response_add_data (VsxResponse *response,
                       uint8_t *buffer,
                       unsigned int buffer_size)
{
  VsxResponseClass *klass =
    (VsxResponseClass *) ((VsxObject *) response)->klass;

  return klass->add_data (response,
                          buffer,
                          buffer_size);
}

gboolean
vsx_response_is_finished (VsxResponse *response)
{
  VsxResponseClass *klass =
    (VsxResponseClass *) ((VsxObject *) response)->klass;

  return klass->is_finished (response);
}

gboolean
vsx_response_has_data (VsxResponse *response)
{
  VsxResponseClass *klass =
    (VsxResponseClass *) ((VsxObject *) response)->klass;

  return klass->has_data (response);
}

void
vsx_response_changed (VsxResponse *response)
{
  vsx_signal_emit (&response->changed_signal, response);
}
