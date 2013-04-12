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

#ifndef __VSX_REQUEST_HANDLER_H__
#define __VSX_REQUEST_HANDLER_H__

#include <glib-object.h>
#include "vsx-response.h"
#include "vsx-conversation-set.h"
#include "vsx-person-set.h"

G_BEGIN_DECLS

#define VSX_TYPE_REQUEST_HANDLER                                        \
  (vsx_request_handler_get_type())
#define VSX_REQUEST_HANDLER(obj)                                        \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                                   \
                               VSX_TYPE_REQUEST_HANDLER,                \
                               VsxRequestHandler))
#define VSX_REQUEST_HANDLER_CLASS(klass)                                \
  (G_TYPE_CHECK_CLASS_CAST ((klass),                                    \
                            VSX_TYPE_REQUEST_HANDLER,                   \
                            VsxRequestHandlerClass))
#define VSX_IS_REQUEST_HANDLER(obj)                                     \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                                   \
                               VSX_TYPE_REQUEST_HANDLER))
#define VSX_IS_REQUEST_HANDLER_CLASS(klass)                             \
  (G_TYPE_CHECK_CLASS_TYPE ((klass),                                    \
                            VSX_TYPE_REQUEST_HANDLER))
#define VSX_REQUEST_HANDLER_GET_CLASS(obj)                              \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                                    \
                              VSX_REQUEST_HANDLER,                      \
                              VsxRequestHandlerClass))

typedef struct _VsxRequestHandler      VsxRequestHandler;
typedef struct _VsxRequestHandlerClass VsxRequestHandlerClass;

typedef enum
{
  VSX_REQUEST_METHOD_GET,
  VSX_REQUEST_METHOD_POST,
  VSX_REQUEST_METHOD_OPTIONS,
  VSX_REQUEST_METHOD_UNKNOWN
} VsxRequestMethod;

struct _VsxRequestHandlerClass
{
  GObjectClass parent_class;

  void
  (* request_line_received) (VsxRequestHandler *handler,
                             VsxRequestMethod method,
                             const char *query_string);

  void
  (* header_received) (VsxRequestHandler *handler,
                       const char *field_name,
                       const char *value);

  void
  (* data_received) (VsxRequestHandler *handler,
                     const guint8 *data,
                     unsigned int length);

  VsxResponse *
  (* request_finished) (VsxRequestHandler *handler);
};

struct _VsxRequestHandler
{
  GObject parent;

  GSocketAddress *socket_address;
  VsxConversationSet *conversation_set;
  VsxPersonSet *person_set;

  VsxRequestMethod request_method;
};

GType
vsx_request_handler_get_type (void) G_GNUC_CONST;

VsxRequestHandler *
vsx_request_handler_new (void);

void
vsx_request_handler_request_line_received (VsxRequestHandler *handler,
                                           VsxRequestMethod method,
                                           const char *query_string);

void
vsx_request_handler_header_received (VsxRequestHandler *handler,
                                     const char *field_name,
                                     const char *value);

void
vsx_request_handler_data_received (VsxRequestHandler *handler,
                                   const guint8 *data,
                                   unsigned int length);

VsxResponse *
vsx_request_handler_request_finished (VsxRequestHandler *handler);

G_END_DECLS

#endif /* __VSX_REQUEST_HANDLER_H__ */
