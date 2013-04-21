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

#include "vsx-start-typing-handler.h"

static void
real_do_request (VsxSimpleHandler *handler,
                 VsxPerson *person)
{
  vsx_conversation_set_typing (person->conversation,
                               person->player->num,
                               TRUE);
}

static const VsxSimpleHandlerClass *
vsx_start_typing_handler_get_class (void)
{
  static VsxSimpleHandlerClass klass;

  if (((VsxObjectClass *) &klass)->free == NULL)
    {
      klass = *vsx_simple_handler_get_class ();

      klass.do_request = real_do_request;
    }

  return &klass;
}

VsxRequestHandler *
vsx_start_typing_handler_new (void)
{
  VsxRequestHandler *handler =
    vsx_object_allocate (vsx_start_typing_handler_get_class ());

  vsx_simple_handler_init (handler);

  return handler;
}
