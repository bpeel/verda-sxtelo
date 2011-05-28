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

#ifndef __GML_NEW_PERSON_RESPONSE_H__
#define __GML_NEW_PERSON_RESPONSE_H__

#include "gml-response.h"
#include "gml-person.h"

G_BEGIN_DECLS

#define GML_TYPE_NEW_PERSON_RESPONSE                                    \
  (gml_new_person_response_get_type())
#define GML_NEW_PERSON_RESPONSE(obj)                                    \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                                   \
                               GML_TYPE_NEW_PERSON_RESPONSE,            \
                               GmlNewPersonResponse))
#define GML_NEW_PERSON_RESPONSE_CLASS(klass)                            \
  (G_TYPE_CHECK_CLASS_CAST ((klass),                                    \
                            GML_TYPE_NEW_PERSON_RESPONSE,               \
                            GmlNewPersonResponseClass))
#define GML_IS_NEW_PERSON_RESPONSE(obj)                                 \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                                   \
                               GML_TYPE_NEW_PERSON_RESPONSE))
#define GML_IS_NEW_PERSON_RESPONSE_CLASS(klass)                         \
  (G_TYPE_CHECK_CLASS_TYPE ((klass),                                    \
                            GML_TYPE_NEW_PERSON_RESPONSE))
#define GML_NEW_PERSON_RESPONSE_GET_CLASS(obj)                          \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                                    \
                              GML_NEW_PERSON_RESPONSE,                  \
                              GmlNewPersonResponseClass))

typedef struct _GmlNewPersonResponse        GmlNewPersonResponse;
typedef struct _GmlNewPersonResponseClass   GmlNewPersonResponseClass;

struct _GmlNewPersonResponseClass
{
  GmlResponseClass parent_class;
};

struct _GmlNewPersonResponse
{
  GmlResponse parent;

  enum
  {
    GML_NEW_PERSON_RESPONSE_HEADERS,
    GML_NEW_PERSON_RESPONSE_BODY,
    GML_NEW_PERSON_RESPONSE_DONE
  } state;

  unsigned int output_pos;

  GmlPerson *person;
};

GType
gml_new_person_response_get_type (void) G_GNUC_CONST;

GmlResponse *
gml_new_person_response_new (GmlPerson *person);

G_END_DECLS

#endif /* __GML_NEW_PERSON_RESPONSE_H__ */

