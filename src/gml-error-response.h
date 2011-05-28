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

#ifndef __GML_ERROR_RESPONSE_H__
#define __GML_ERROR_RESPONSE_H__

#include "gml-response.h"

G_BEGIN_DECLS

#define GML_TYPE_ERROR_RESPONSE                 \
  (gml_error_response_get_type())
#define GML_ERROR_RESPONSE(obj)                         \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                   \
                               GML_TYPE_ERROR_RESPONSE, \
                               GmlErrorResponse))
#define GML_ERROR_RESPONSE_CLASS(klass)                 \
  (G_TYPE_CHECK_CLASS_CAST ((klass),                    \
                            GML_TYPE_ERROR_RESPONSE,    \
                            GmlErrorResponseClass))
#define GML_IS_ERROR_RESPONSE(obj)                              \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                           \
                               GML_TYPE_ERROR_RESPONSE))
#define GML_IS_ERROR_RESPONSE_CLASS(klass)              \
  (G_TYPE_CHECK_CLASS_TYPE ((klass),                    \
                            GML_TYPE_ERROR_RESPONSE))
#define GML_ERROR_RESPONSE_GET_CLASS(obj)               \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                    \
                              GML_ERROR_RESPONSE,       \
                              GmlErrorResponseClass))

typedef struct _GmlErrorResponse        GmlErrorResponse;
typedef struct _GmlErrorResponseClass   GmlErrorResponseClass;

typedef enum
{
  GML_ERROR_RESPONSE_BAD_REQUEST,
  GML_ERROR_RESPONSE_UNSUPPORTED_REQUEST,
  GML_ERROR_RESPONSE_NOT_FOUND
} GmlErrorResponseType;

struct _GmlErrorResponseClass
{
  GmlResponseClass parent_class;
};

struct _GmlErrorResponse
{
  GmlResponse parent;

  GmlErrorResponseType type;

  unsigned int output_pos;
};

GType
gml_error_response_get_type (void) G_GNUC_CONST;

GmlResponse *
gml_error_response_new (GmlErrorResponseType type);

G_END_DECLS

#endif /* __GML_ERROR_RESPONSE_H__ */

