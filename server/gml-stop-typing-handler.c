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

#include "gml-stop-typing-handler.h"
#include "gml-string-response.h"

G_DEFINE_TYPE (GmlStopTypingHandler,
               gml_stop_typing_handler,
               GML_TYPE_REQUEST_HANDLER);

static void
real_dispose (GObject *object)
{
  GmlStopTypingHandler *handler = GML_STOP_TYPING_HANDLER (object);

  if (handler->person)
    {
      g_object_unref (handler->person);
      handler->person = NULL;
    }

  if (handler->response)
    {
      g_object_unref (handler->response);
      handler->response = NULL;
    }

  G_OBJECT_CLASS (gml_stop_typing_handler_parent_class)->dispose (object);
}

static void
real_request_line_received (GmlRequestHandler *handler,
                            GmlRequestMethod method,
                            const char *query_string)
{
  GmlStopTypingHandler *self = GML_STOP_TYPING_HANDLER (handler);
  GmlPersonId id;

  if (method == GML_REQUEST_METHOD_GET
      && query_string != NULL
      && gml_person_parse_id (query_string, &id))
    {
      self->person = gml_person_set_get_person (handler->person_set, id);

      if (self->person == NULL)
        self->response =
          gml_string_response_new (GML_STRING_RESPONSE_NOT_FOUND);
      else
        g_object_ref (self->person);
    }
  else
    self->response = gml_string_response_new (GML_STRING_RESPONSE_BAD_REQUEST);
}

static GmlResponse *
real_request_finished (GmlRequestHandler *handler)
{
  GmlStopTypingHandler *self = GML_STOP_TYPING_HANDLER (handler);

  if (self->person)
    {
      if (self->person->conversation)
        gml_conversation_set_typing (self->person->conversation,
                                     self->person->person_num,
                                     FALSE);

      return gml_string_response_new (GML_STRING_RESPONSE_OK);
    }
  else if (self->response)
    return g_object_ref (self->response);
  else
    {
      g_warn_if_reached ();

      return gml_string_response_new (GML_STRING_RESPONSE_BAD_REQUEST);
    }
}

static void
gml_stop_typing_handler_class_init (GmlStopTypingHandlerClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  GmlRequestHandlerClass *request_handler_class
    = (GmlRequestHandlerClass *) klass;

  object_class->dispose = real_dispose;

  request_handler_class->request_line_received = real_request_line_received;
  request_handler_class->request_finished = real_request_finished;
}

static void
gml_stop_typing_handler_init (GmlStopTypingHandler *self)
{
}
