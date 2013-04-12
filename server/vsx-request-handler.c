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

#include "vsx-request-handler.h"
#include "vsx-string-response.h"

G_DEFINE_TYPE (VsxRequestHandler, vsx_request_handler, G_TYPE_OBJECT);

static void
vsx_request_handler_dispose (GObject *object)
{
  VsxRequestHandler *handler = VSX_REQUEST_HANDLER (object);

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

  G_OBJECT_CLASS (vsx_request_handler_parent_class)->dispose (object);
}

static void
vsx_request_handler_real_request_line_received (VsxRequestHandler *handler,
                                                VsxRequestMethod method,
                                                const char *query_string)
{
  handler->request_method = method;

  /* Default handler just ignores everything */
}

static void
vsx_request_handler_real_header_received (VsxRequestHandler *handler,
                                          const char *field_name,
                                          const char *value)
{
  /* Default handler just ignores everything */
}

static void
vsx_request_handler_real_data_received (VsxRequestHandler *handler,
                                        const guint8 *data,
                                        unsigned int length)
{
  /* Default handler just ignores everything */
}

static VsxResponse *
vsx_request_handler_real_request_finished (VsxRequestHandler *handler)
{
  /* Default handler assumes this is an unknown resource */
  if (handler->request_method == VSX_REQUEST_METHOD_UNKNOWN)
    return vsx_string_response_new (VSX_STRING_RESPONSE_UNSUPPORTED_REQUEST);
  else
    return vsx_string_response_new (VSX_STRING_RESPONSE_NOT_FOUND);
}

static void
vsx_request_handler_class_init (VsxRequestHandlerClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  VsxRequestHandlerClass *request_handler_class
    = (VsxRequestHandlerClass *) klass;

  object_class->dispose = vsx_request_handler_dispose;

  request_handler_class->request_line_received
    = vsx_request_handler_real_request_line_received;
  request_handler_class->header_received
    = vsx_request_handler_real_header_received;
  request_handler_class->data_received
    = vsx_request_handler_real_data_received;
  request_handler_class->request_finished
    = vsx_request_handler_real_request_finished;
}

static void
vsx_request_handler_init (VsxRequestHandler *self)
{
}

VsxRequestHandler *
vsx_request_handler_new (void)
{
  VsxRequestHandler *self = g_object_new (VSX_TYPE_REQUEST_HANDLER, NULL);

  return self;
}

void
vsx_request_handler_request_line_received (VsxRequestHandler *handler,
                                           VsxRequestMethod method,
                                           const char *query_string)
{
  VSX_REQUEST_HANDLER_GET_CLASS (handler)
    ->request_line_received (handler, method, query_string);
}

void
vsx_request_handler_header_received (VsxRequestHandler *handler,
                                     const char *field_name,
                                     const char *value)
{
  VSX_REQUEST_HANDLER_GET_CLASS (handler)
    ->header_received (handler, field_name, value);
}

void
vsx_request_handler_data_received (VsxRequestHandler *handler,
                                   const guint8 *data,
                                   unsigned int length)
{
  VSX_REQUEST_HANDLER_GET_CLASS (handler)
    ->data_received (handler, data, length);
}

VsxResponse *
vsx_request_handler_request_finished (VsxRequestHandler *handler)
{
  return VSX_REQUEST_HANDLER_GET_CLASS (handler)->request_finished (handler);
}
