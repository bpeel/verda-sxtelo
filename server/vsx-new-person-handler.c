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

#include "vsx-new-person-handler.h"
#include "vsx-string-response.h"
#include "vsx-watch-person-response.h"
#include "vsx-arguments.h"

G_DEFINE_TYPE (VsxNewPersonHandler,
               vsx_new_person_handler,
               VSX_TYPE_REQUEST_HANDLER);

static void
real_finalize (GObject *object)
{
  VsxNewPersonHandler *handler = VSX_NEW_PERSON_HANDLER (object);

  g_free (handler->room_name);
  g_free (handler->player_name);

  G_OBJECT_CLASS (vsx_new_person_handler_parent_class)->finalize (object);
}

static void
real_request_line_received (VsxRequestHandler *handler,
                            VsxRequestMethod method,
                            const char *query_string)
{
  VsxNewPersonHandler *self = VSX_NEW_PERSON_HANDLER (handler);

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
  VsxNewPersonHandler *self = VSX_NEW_PERSON_HANDLER (handler);
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

      response = vsx_watch_person_response_new (person, 0);

      g_object_unref (conversation);
      g_object_unref (person);
    }
  else
    response = vsx_string_response_new (VSX_STRING_RESPONSE_BAD_REQUEST);

  return response;
}

static void
vsx_new_person_handler_class_init (VsxNewPersonHandlerClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  VsxRequestHandlerClass *request_handler_class
    = (VsxRequestHandlerClass *) klass;

  object_class->finalize = real_finalize;

  request_handler_class->request_line_received = real_request_line_received;
  request_handler_class->request_finished = real_request_finished;
}

static void
vsx_new_person_handler_init (VsxNewPersonHandler *self)
{
}
