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

#ifndef __GML_WATCH_PERSON_RESPONSE_H__
#define __GML_WATCH_PERSON_RESPONSE_H__

#include "gml-response.h"
#include "gml-person.h"

G_BEGIN_DECLS

#define GML_TYPE_WATCH_PERSON_RESPONSE          \
  (gml_watch_person_response_get_type())
#define GML_WATCH_PERSON_RESPONSE(obj)                          \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                           \
                               GML_TYPE_WATCH_PERSON_RESPONSE,  \
                               GmlWatchPersonResponse))
#define GML_WATCH_PERSON_RESPONSE_CLASS(klass)                  \
  (G_TYPE_CHECK_CLASS_CAST ((klass),                            \
                            GML_TYPE_WATCH_PERSON_RESPONSE,     \
                            GmlWatchPersonResponseClass))
#define GML_IS_WATCH_PERSON_RESPONSE(obj)                       \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                           \
                               GML_TYPE_WATCH_PERSON_RESPONSE))
#define GML_IS_WATCH_PERSON_RESPONSE_CLASS(klass)               \
  (G_TYPE_CHECK_CLASS_TYPE ((klass),                            \
                            GML_TYPE_WATCH_PERSON_RESPONSE))
#define GML_WATCH_PERSON_RESPONSE_GET_CLASS(obj)                \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                            \
                              GML_WATCH_PERSON_RESPONSE,        \
                              GmlWatchPersonResponseClass))

typedef struct _GmlWatchPersonResponse        GmlWatchPersonResponse;
typedef struct _GmlWatchPersonResponseClass   GmlWatchPersonResponseClass;

struct _GmlWatchPersonResponseClass
{
  GmlResponseClass parent_class;
};

struct _GmlWatchPersonResponse
{
  GmlResponse parent;

  GmlPerson *person;

  guint person_changed_handler;

  enum
  {
    GML_WATCH_PERSON_RESPONSE_WRITING_HTTP_HEADER,
    GML_WATCH_PERSON_RESPONSE_WRITING_HEADER_START,
    GML_WATCH_PERSON_RESPONSE_WRITING_HEADER_ID,
    GML_WATCH_PERSON_RESPONSE_WRITING_HEADER_END,
    GML_WATCH_PERSON_RESPONSE_AWAITING_START,
    GML_WATCH_PERSON_RESPONSE_WRITING_START,
    GML_WATCH_PERSON_RESPONSE_WRITING_MESSAGES,
    GML_WATCH_PERSON_RESPONSE_WRITING_END,
    GML_WATCH_PERSON_RESPONSE_DONE
  } state;

  unsigned int message_num;
  unsigned int message_pos;
};

GType
gml_watch_person_response_get_type (void) G_GNUC_CONST;

GmlResponse *
gml_watch_person_response_new (GmlPerson *person);

G_END_DECLS

#endif /* __GML_WATCH_PERSON_RESPONSE_H__ */

