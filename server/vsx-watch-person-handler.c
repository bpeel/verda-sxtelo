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

#include <glib-object.h>

#include "vsx-watch-person-handler.h"
#include "vsx-string-response.h"
#include "vsx-watch-person-response.h"
#include "vsx-arguments.h"

G_DEFINE_TYPE (VsxWatchPersonHandler,
               vsx_watch_person_handler,
               VSX_TYPE_REQUEST_HANDLER);

static void
real_dispose (GObject *object)
{
  VsxWatchPersonHandler *handler = VSX_WATCH_PERSON_HANDLER (object);

  if (handler->response)
    {
      g_object_unref (handler->response);
      handler->response = NULL;
    }

  G_OBJECT_CLASS (vsx_watch_person_handler_parent_class)->dispose (object);
}

static void
real_request_line_received (VsxRequestHandler *handler,
                            VsxRequestMethod method,
                            const char *query_string)
{
  VsxWatchPersonHandler *self = VSX_WATCH_PERSON_HANDLER (handler);
  VsxPersonId id;
  int last_message;

  if (method == VSX_REQUEST_METHOD_GET
      && vsx_arguments_parse ("pi",
                              query_string,
                              &id,
                              &last_message))
    {
      VsxPerson *person = vsx_person_set_get_person (handler->person_set, id);

      if (person == NULL)
        self->response
          = vsx_string_response_new (VSX_STRING_RESPONSE_NOT_FOUND);
      else if (last_message < 0
               || last_message > person->conversation->messages->len)
        self->response
          = vsx_string_response_new (VSX_STRING_RESPONSE_BAD_REQUEST);
      else
        {
          vsx_person_make_noise (person);
          self->response = vsx_watch_person_response_new (person, last_message);
        }
    }
  else
    self->response = vsx_string_response_new (VSX_STRING_RESPONSE_BAD_REQUEST);
}

static VsxResponse *
real_request_finished (VsxRequestHandler *handler)
{
  VsxWatchPersonHandler *self = VSX_WATCH_PERSON_HANDLER (handler);

  if (self->response)
    return g_object_ref (self->response);
  else
    {
      g_warn_if_reached ();

      return vsx_string_response_new (VSX_STRING_RESPONSE_BAD_REQUEST);
    }
}

static void
vsx_watch_person_handler_class_init (VsxWatchPersonHandlerClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  VsxRequestHandlerClass *request_handler_class
    = (VsxRequestHandlerClass *) klass;

  object_class->dispose = real_dispose;

  request_handler_class->request_line_received = real_request_line_received;
  request_handler_class->request_finished = real_request_finished;
}

static void
vsx_watch_person_handler_init (VsxWatchPersonHandler *self)
{
}
