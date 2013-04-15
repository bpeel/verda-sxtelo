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

#include "vsx-request-handler.h"
#include "vsx-string-response.h"

static void
vsx_request_handler_free (void *object)
{
  VsxRequestHandler *handler = object;

  if (handler->socket_address)
    g_object_unref (handler->socket_address);

  if (handler->person_set)
    vsx_object_unref (handler->person_set);

  if (handler->conversation_set)
    vsx_object_unref (handler->conversation_set);

  vsx_object_get_class ()->free (object);
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

const VsxRequestHandlerClass *
vsx_request_handler_get_class (void)
{
  static VsxRequestHandlerClass klass;

  if (klass.parent_class.free == NULL)
    {
      klass.parent_class = *vsx_object_get_class ();
      klass.parent_class.instance_size = sizeof (VsxRequestHandler);
      klass.parent_class.free = vsx_request_handler_free;

      klass.request_line_received =
        vsx_request_handler_real_request_line_received;
      klass.header_received = vsx_request_handler_real_header_received;
      klass.data_received = vsx_request_handler_real_data_received;
      klass.request_finished = vsx_request_handler_real_request_finished;
    }

  return &klass;
}

void
vsx_request_handler_init (void *object)
{
  vsx_object_init (object);
}

VsxRequestHandler *
vsx_request_handler_new (void)
{
  VsxRequestHandler *self =
    vsx_object_allocate (vsx_request_handler_get_class ());

  vsx_request_handler_init (self);

  return self;
}

void
vsx_request_handler_request_line_received (VsxRequestHandler *handler,
                                           VsxRequestMethod method,
                                           const char *query_string)
{
  VsxRequestHandlerClass *klass =
    (VsxRequestHandlerClass *) ((VsxObject *) handler)->klass;

  klass->request_line_received (handler, method, query_string);
}

void
vsx_request_handler_header_received (VsxRequestHandler *handler,
                                     const char *field_name,
                                     const char *value)
{
  VsxRequestHandlerClass *klass =
    (VsxRequestHandlerClass *) ((VsxObject *) handler)->klass;

  klass->header_received (handler, field_name, value);
}

void
vsx_request_handler_data_received (VsxRequestHandler *handler,
                                   const guint8 *data,
                                   unsigned int length)
{
  VsxRequestHandlerClass *klass =
    (VsxRequestHandlerClass *) ((VsxObject *) handler)->klass;

  klass->data_received (handler, data, length);
}

VsxResponse *
vsx_request_handler_request_finished (VsxRequestHandler *handler)
{
  VsxRequestHandlerClass *klass =
    (VsxRequestHandlerClass *) ((VsxObject *) handler)->klass;

  return klass->request_finished (handler);
}
