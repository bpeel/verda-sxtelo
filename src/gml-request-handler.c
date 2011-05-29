/*
 * Gemelo - A person for chatting with strangers in a foreign language
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

#include "gml-request-handler.h"
#include "gml-string-response.h"

G_DEFINE_TYPE (GmlRequestHandler, gml_request_handler, G_TYPE_OBJECT);

static void
gml_request_handler_dispose (GObject *object)
{
  GmlRequestHandler *handler = GML_REQUEST_HANDLER (object);

  if (handler->socket_address)
    {
      g_object_unref (handler->socket_address);
      handler->socket_address = NULL;
    }

  if (handler->person_set)
    {
      g_object_unref (handler->person_set);
      handler->person_set = NULL;
    }

  if (handler->conversation_set)
    {
      g_object_unref (handler->conversation_set);
      handler->conversation_set = NULL;
    }

  G_OBJECT_CLASS (gml_request_handler_parent_class)->dispose (object);
}

static void
gml_request_handler_real_request_line_received (GmlRequestHandler *handler,
                                                GmlRequestMethod method,
                                                const char *query_string)
{
  handler->request_method = method;

  /* Default handler just ignores everything */
}

static void
gml_request_handler_real_header_received (GmlRequestHandler *handler,
                                          const char *field_name,
                                          const char *value)
{
  /* Default handler just ignores everything */
}

static void
gml_request_handler_real_data_received (GmlRequestHandler *handler,
                                        const guint8 *data,
                                        unsigned int length)
{
  /* Default handler just ignores everything */
}

static GmlResponse *
gml_request_handler_real_request_finished (GmlRequestHandler *handler)
{
  /* Default handler assumes this is an unknown resource */
  if (handler->request_method == GML_REQUEST_METHOD_UNKNOWN)
    return gml_string_response_new (GML_STRING_RESPONSE_UNSUPPORTED_REQUEST);
  else
    return gml_string_response_new (GML_STRING_RESPONSE_NOT_FOUND);
}

static void
gml_request_handler_class_init (GmlRequestHandlerClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  GmlRequestHandlerClass *request_handler_class
    = (GmlRequestHandlerClass *) klass;

  object_class->dispose = gml_request_handler_dispose;

  request_handler_class->request_line_received
    = gml_request_handler_real_request_line_received;
  request_handler_class->header_received
    = gml_request_handler_real_header_received;
  request_handler_class->data_received
    = gml_request_handler_real_data_received;
  request_handler_class->request_finished
    = gml_request_handler_real_request_finished;
}

static void
gml_request_handler_init (GmlRequestHandler *self)
{
}

GmlRequestHandler *
gml_request_handler_new (void)
{
  GmlRequestHandler *self = g_object_new (GML_TYPE_REQUEST_HANDLER, NULL);

  return self;
}

void
gml_request_handler_request_line_received (GmlRequestHandler *handler,
                                           GmlRequestMethod method,
                                           const char *query_string)
{
  GML_REQUEST_HANDLER_GET_CLASS (handler)
    ->request_line_received (handler, method, query_string);
}

void
gml_request_handler_header_received (GmlRequestHandler *handler,
                                     const char *field_name,
                                     const char *value)
{
  GML_REQUEST_HANDLER_GET_CLASS (handler)
    ->header_received (handler, field_name, value);
}

void
gml_request_handler_data_received (GmlRequestHandler *handler,
                                   const guint8 *data,
                                   unsigned int length)
{
  GML_REQUEST_HANDLER_GET_CLASS (handler)
    ->data_received (handler, data, length);
}

GmlResponse *
gml_request_handler_request_finished (GmlRequestHandler *handler)
{
  return GML_REQUEST_HANDLER_GET_CLASS (handler)->request_finished (handler);
}
