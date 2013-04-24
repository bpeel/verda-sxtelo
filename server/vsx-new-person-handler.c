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

#include "vsx-new-person-handler.h"
#include "vsx-string-response.h"
#include "vsx-watch-person-response.h"
#include "vsx-arguments.h"

static void
real_free (void *object)
{
  VsxNewPersonHandler *handler = (VsxNewPersonHandler *) object;

  g_free (handler->room_name);
  g_free (handler->player_name);

  vsx_request_handler_get_class ()->parent_class.free (object);
}

static void
real_request_line_received (VsxRequestHandler *handler,
                            VsxRequestMethod method,
                            const char *query_string)
{
  VsxNewPersonHandler *self = (VsxNewPersonHandler *) handler;

  if (method != VSX_REQUEST_METHOD_GET)
    return;

  if (!vsx_arguments_parse ("nn",
                            query_string,
                            &self->room_name,
                            &self->player_name))
    {
      self->room_name = NULL;
      self->player_name = NULL;
    }
}

static VsxResponse *
real_request_finished (VsxRequestHandler *handler)
{
  VsxNewPersonHandler *self = (VsxNewPersonHandler *) handler;
  VsxResponse *response;

  if (self->room_name && self->player_name)
    {
      VsxConversation *conversation;
      VsxPerson *person;

      conversation =
        vsx_conversation_set_get_conversation (handler->conversation_set,
                                               self->room_name);
      person = vsx_person_set_generate_person (handler->person_set,
                                               self->player_name,
                                               handler->socket_address,
                                               conversation);

      if (conversation->n_players == 1)
        vsx_log ("New player “%s” created new game in “%s”",
                 self->player_name,
                 self->room_name);
      else
        vsx_log ("New player “%s” joined game in “%s”",
                 self->player_name,
                 self->room_name);

      response = vsx_watch_person_response_new (person, 0);

      vsx_object_unref (conversation);
      vsx_object_unref (person);
    }
  else
    response = vsx_string_response_new (VSX_STRING_RESPONSE_BAD_REQUEST);

  return response;
}

static const VsxRequestHandlerClass *
vsx_new_person_handler_get_class (void)
{
  static VsxRequestHandlerClass klass;

  if (klass.parent_class.free == NULL)
    {
      klass = *vsx_request_handler_get_class ();
      klass.parent_class.instance_size = sizeof (VsxNewPersonHandler);
      klass.parent_class.free = real_free;

      klass.request_line_received = real_request_line_received;
      klass.request_finished = real_request_finished;
    }

  return &klass;
}

VsxRequestHandler *
vsx_new_person_handler_new (void)
{
  VsxRequestHandler *handler =
    vsx_object_allocate (vsx_new_person_handler_get_class ());

  vsx_request_handler_init (handler);

  return handler;
}
