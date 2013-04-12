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

#ifndef __VSX_WATCH_PERSON_RESPONSE_H__
#define __VSX_WATCH_PERSON_RESPONSE_H__

#include "vsx-response.h"
#include "vsx-person.h"

G_BEGIN_DECLS

#define VSX_TYPE_WATCH_PERSON_RESPONSE          \
  (vsx_watch_person_response_get_type())
#define VSX_WATCH_PERSON_RESPONSE(obj)                          \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                           \
                               VSX_TYPE_WATCH_PERSON_RESPONSE,  \
                               VsxWatchPersonResponse))
#define VSX_WATCH_PERSON_RESPONSE_CLASS(klass)                  \
  (G_TYPE_CHECK_CLASS_CAST ((klass),                            \
                            VSX_TYPE_WATCH_PERSON_RESPONSE,     \
                            VsxWatchPersonResponseClass))
#define VSX_IS_WATCH_PERSON_RESPONSE(obj)                       \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                           \
                               VSX_TYPE_WATCH_PERSON_RESPONSE))
#define VSX_IS_WATCH_PERSON_RESPONSE_CLASS(klass)               \
  (G_TYPE_CHECK_CLASS_TYPE ((klass),                            \
                            VSX_TYPE_WATCH_PERSON_RESPONSE))
#define VSX_WATCH_PERSON_RESPONSE_GET_CLASS(obj)                \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                            \
                              VSX_WATCH_PERSON_RESPONSE,        \
                              VsxWatchPersonResponseClass))

typedef struct _VsxWatchPersonResponse        VsxWatchPersonResponse;
typedef struct _VsxWatchPersonResponseClass   VsxWatchPersonResponseClass;

struct _VsxWatchPersonResponseClass
{
  VsxResponseClass parent_class;
};

struct _VsxWatchPersonResponse
{
  VsxResponse parent;

  VsxPerson *person;

  guint person_changed_handler;

  enum
  {
    VSX_WATCH_PERSON_RESPONSE_WRITING_HTTP_HEADER,
    VSX_WATCH_PERSON_RESPONSE_WRITING_HEADER_START,
    VSX_WATCH_PERSON_RESPONSE_WRITING_HEADER_ID,
    VSX_WATCH_PERSON_RESPONSE_WRITING_HEADER_END,
    VSX_WATCH_PERSON_RESPONSE_AWAITING_START,
    VSX_WATCH_PERSON_RESPONSE_WRITING_START,
    VSX_WATCH_PERSON_RESPONSE_WRITING_MESSAGES,
    VSX_WATCH_PERSON_RESPONSE_WRITING_END,
    VSX_WATCH_PERSON_RESPONSE_DONE
  } state;

  unsigned int message_num;
  unsigned int message_pos;

  gboolean last_typing_state;
};

GType
vsx_watch_person_response_get_type (void) G_GNUC_CONST;

VsxResponse *
vsx_watch_person_response_new (VsxPerson *person);

G_END_DECLS

#endif /* __VSX_WATCH_PERSON_RESPONSE_H__ */

