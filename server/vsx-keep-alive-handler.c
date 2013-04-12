/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2012  Neil Roberts
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

#include "vsx-keep-alive-handler.h"
#include "vsx-string-response.h"

G_DEFINE_TYPE (VsxKeepAliveHandler,
               vsx_keep_alive_handler,
               VSX_TYPE_REQUEST_HANDLER);

static void
real_dispose (GObject *object)
{
  VsxKeepAliveHandler *handler = VSX_KEEP_ALIVE_HANDLER (object);

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

  G_OBJECT_CLASS (vsx_keep_alive_handler_parent_class)->dispose (object);
}

static void
real_request_line_received (VsxRequestHandler *handler,
                            VsxRequestMethod method,
                            const char *query_string)
{
  VsxKeepAliveHandler *self = VSX_KEEP_ALIVE_HANDLER (handler);
  VsxPersonId id;

  if (method == VSX_REQUEST_METHOD_GET
      && query_string != NULL
      && vsx_person_parse_id (query_string, &id))
    {
      self->person = vsx_person_set_activate_person (handler->person_set, id);

      if (self->person == NULL)
        self->response =
          vsx_string_response_new (VSX_STRING_RESPONSE_NOT_FOUND);
      else
        g_object_ref (self->person);
    }
  else
    self->response = vsx_string_response_new (VSX_STRING_RESPONSE_BAD_REQUEST);
}

static VsxResponse *
real_request_finished (VsxRequestHandler *handler)
{
  VsxKeepAliveHandler *self = VSX_KEEP_ALIVE_HANDLER (handler);

  if (self->person)
    return vsx_string_response_new (VSX_STRING_RESPONSE_OK);
  else if (self->response)
    return g_object_ref (self->response);
  else
    {
      g_warn_if_reached ();

      return vsx_string_response_new (VSX_STRING_RESPONSE_BAD_REQUEST);
    }
}

static void
vsx_keep_alive_handler_class_init (VsxKeepAliveHandlerClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  VsxRequestHandlerClass *request_handler_class
    = (VsxRequestHandlerClass *) klass;

  object_class->dispose = real_dispose;

  request_handler_class->request_line_received = real_request_line_received;
  request_handler_class->request_finished = real_request_finished;
}

static void
vsx_keep_alive_handler_init (VsxKeepAliveHandler *self)
{
}
