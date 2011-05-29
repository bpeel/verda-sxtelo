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

#ifndef __GML_NEW_PERSON_HANDLER_H__
#define __GML_NEW_PERSON_HANDLER_H__

#include <glib-object.h>
#include <gio/gio.h>
#include "gml-request-handler.h"
#include "gml-conversation-set.h"
#include "gml-person-set.h"

G_BEGIN_DECLS

#define GML_TYPE_NEW_PERSON_HANDLER             \
  (gml_new_person_handler_get_type())
#define GML_NEW_PERSON_HANDLER(obj)                             \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                           \
                               GML_TYPE_NEW_PERSON_HANDLER,     \
                               GmlNewPersonHandler))
#define GML_NEW_PERSON_HANDLER_CLASS(klass)                     \
  (G_TYPE_CHECK_CLASS_CAST ((klass),                            \
                            GML_TYPE_NEW_PERSON_HANDLER,        \
                            GmlNewPersonHandlerClass))
#define GML_IS_NEW_PERSON_HANDLER(obj)                          \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                           \
                               GML_TYPE_NEW_PERSON_HANDLER))
#define GML_IS_NEW_PERSON_HANDLER_CLASS(klass)                  \
  (G_TYPE_CHECK_CLASS_TYPE ((klass),                            \
                            GML_TYPE_NEW_PERSON_HANDLER))
#define GML_NEW_PERSON_HANDLER_GET_CLASS(obj)                   \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                            \
                              GML_NEW_PERSON_HANDLER,           \
                              GmlNewPersonHandlerClass))

typedef struct _GmlNewPersonHandler      GmlNewPersonHandler;
typedef struct _GmlNewPersonHandlerClass GmlNewPersonHandlerClass;

struct _GmlNewPersonHandlerClass
{
  GmlRequestHandlerClass parent_class;
};

struct _GmlNewPersonHandler
{
  GmlRequestHandler parent;

  char *room_name;
};

GType
gml_new_person_handler_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __GML_NEW_PERSON_HANDLER_H__ */
