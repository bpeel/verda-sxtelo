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

#ifndef __GML_STRING_RESPONSE_H__
#define __GML_STRING_RESPONSE_H__

#include "gml-response.h"

G_BEGIN_DECLS

#define GML_TYPE_STRING_RESPONSE                \
  (gml_string_response_get_type())
#define GML_STRING_RESPONSE(obj)                                \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                           \
                               GML_TYPE_STRING_RESPONSE,        \
                               GmlStringResponse))
#define GML_STRING_RESPONSE_CLASS(klass)                \
  (G_TYPE_CHECK_CLASS_CAST ((klass),                    \
                            GML_TYPE_STRING_RESPONSE,   \
                            GmlStringResponseClass))
#define GML_IS_STRING_RESPONSE(obj)                             \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                           \
                               GML_TYPE_STRING_RESPONSE))
#define GML_IS_STRING_RESPONSE_CLASS(klass)             \
  (G_TYPE_CHECK_CLASS_TYPE ((klass),                    \
                            GML_TYPE_STRING_RESPONSE))
#define GML_STRING_RESPONSE_GET_CLASS(obj)              \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                    \
                              GML_STRING_RESPONSE,      \
                              GmlStringResponseClass))

typedef struct _GmlStringResponse        GmlStringResponse;
typedef struct _GmlStringResponseClass   GmlStringResponseClass;

typedef enum
{
  GML_STRING_RESPONSE_BAD_REQUEST,
  GML_STRING_RESPONSE_UNSUPPORTED_REQUEST,
  GML_STRING_RESPONSE_NOT_FOUND,
  GML_STRING_RESPONSE_REQUEST_TIMEOUT,
  GML_STRING_RESPONSE_PREFLIGHT_POST_OK,
  GML_STRING_RESPONSE_OK
} GmlStringResponseType;

struct _GmlStringResponseClass
{
  GmlResponseClass parent_class;
};

struct _GmlStringResponse
{
  GmlResponse parent;

  GmlStringResponseType type;

  unsigned int output_pos;
};

GType
gml_string_response_get_type (void) G_GNUC_CONST;

GmlResponse *
gml_string_response_new (GmlStringResponseType type);

G_END_DECLS

#endif /* __GML_STRING_RESPONSE_H__ */

