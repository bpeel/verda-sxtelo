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

#include "gml-new-person-handler.h"
#include "gml-string-response.h"
#include "gml-watch-person-response.h"

G_DEFINE_TYPE (GmlNewPersonHandler,
               gml_new_person_handler,
               GML_TYPE_REQUEST_HANDLER);

static void
real_finalize (GObject *object)
{
  GmlNewPersonHandler *handler = GML_NEW_PERSON_HANDLER (object);

  g_free (handler->room_name);

  G_OBJECT_CLASS (gml_new_person_handler_parent_class)->finalize (object);
}

static void
real_request_line_received (GmlRequestHandler *handler,
                            GmlRequestMethod method,
                            const char *query_string)
{
  GmlNewPersonHandler *self = GML_NEW_PERSON_HANDLER (handler);
  const char *p;

  if (method != GML_REQUEST_METHOD_GET)
    return;

  /* The query string will be used as the room name. It should only
     contain letters */
  if (query_string == NULL || *query_string == 0)
    return;

  for (p = query_string; *p; p++)
    if (!g_ascii_isalpha (*p))
      return;

  self->room_name = g_strdup (query_string);
}

static GmlResponse *
real_request_finished (GmlRequestHandler *handler)
{
  GmlNewPersonHandler *self = GML_NEW_PERSON_HANDLER (handler);
  GmlResponse *response;

  if (self->room_name)
    {
      GmlConversation *conversation;
      GmlPerson *person;

      conversation =
        gml_conversation_set_get_conversation (handler->conversation_set,
                                               self->room_name);
      person = gml_person_set_generate_person (handler->person_set,
                                               handler->socket_address,
                                               conversation);

      response = gml_watch_person_response_new (person);

      g_object_unref (conversation);
      g_object_unref (person);
    }
  else
    response = gml_string_response_new (GML_STRING_RESPONSE_BAD_REQUEST);

  return response;
}

static void
gml_new_person_handler_class_init (GmlNewPersonHandlerClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  GmlRequestHandlerClass *request_handler_class
    = (GmlRequestHandlerClass *) klass;

  object_class->finalize = real_finalize;

  request_handler_class->request_line_received = real_request_line_received;
  request_handler_class->request_finished = real_request_finished;
}

static void
gml_new_person_handler_init (GmlNewPersonHandler *self)
{
}
