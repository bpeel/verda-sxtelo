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

#ifndef __GML_REQUEST_HANDLER_H__
#define __GML_REQUEST_HANDLER_H__

#include <glib-object.h>
#include "gml-response.h"
#include "gml-conversation-set.h"
#include "gml-person-set.h"

G_BEGIN_DECLS

#define GML_TYPE_REQUEST_HANDLER                                        \
  (gml_request_handler_get_type())
#define GML_REQUEST_HANDLER(obj)                                        \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                                   \
                               GML_TYPE_REQUEST_HANDLER,                \
                               GmlRequestHandler))
#define GML_REQUEST_HANDLER_CLASS(klass)                                \
  (G_TYPE_CHECK_CLASS_CAST ((klass),                                    \
                            GML_TYPE_REQUEST_HANDLER,                   \
                            GmlRequestHandlerClass))
#define GML_IS_REQUEST_HANDLER(obj)                                     \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                                   \
                               GML_TYPE_REQUEST_HANDLER))
#define GML_IS_REQUEST_HANDLER_CLASS(klass)                             \
  (G_TYPE_CHECK_CLASS_TYPE ((klass),                                    \
                            GML_TYPE_REQUEST_HANDLER))
#define GML_REQUEST_HANDLER_GET_CLASS(obj)                              \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                                    \
                              GML_REQUEST_HANDLER,                      \
                              GmlRequestHandlerClass))

typedef struct _GmlRequestHandler      GmlRequestHandler;
typedef struct _GmlRequestHandlerClass GmlRequestHandlerClass;

typedef enum
{
  GML_REQUEST_METHOD_GET,
  GML_REQUEST_METHOD_POST,
  GML_REQUEST_METHOD_UNKNOWN
} GmlRequestMethod;

struct _GmlRequestHandlerClass
{
  GObjectClass parent_class;

  void
  (* request_line_received) (GmlRequestHandler *handler,
                             GmlRequestMethod method,
                             const char *query_string);

  void
  (* header_received) (GmlRequestHandler *handler,
                       const char *field_name,
                       const char *value);

  void
  (* data_received) (GmlRequestHandler *handler,
                     const guint8 *data,
                     unsigned int length);

  GmlResponse *
  (* request_finished) (GmlRequestHandler *handler);
};

struct _GmlRequestHandler
{
  GObject parent;

  GSocketAddress *socket_address;
  GmlConversationSet *conversation_set;
  GmlPersonSet *person_set;

  GmlRequestMethod request_method;
};

GType
gml_request_handler_get_type (void) G_GNUC_CONST;

GmlRequestHandler *
gml_request_handler_new (void);

void
gml_request_handler_request_line_received (GmlRequestHandler *handler,
                                           GmlRequestMethod method,
                                           const char *query_string);

void
gml_request_handler_header_received (GmlRequestHandler *handler,
                                     const char *field_name,
                                     const char *value);

void
gml_request_handler_data_received (GmlRequestHandler *handler,
                                   const guint8 *data,
                                   unsigned int length);

GmlResponse *
gml_request_handler_request_finished (GmlRequestHandler *handler);

G_END_DECLS

#endif /* __GML_REQUEST_HANDLER_H__ */
