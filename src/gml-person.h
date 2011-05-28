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

#ifndef __GML_PERSON_H__
#define __GML_PERSON_H__

#include <glib-object.h>
#include <gio/gio.h>
#include "gml-conversation.h"

G_BEGIN_DECLS

#define GML_TYPE_PERSON                                                 \
  (gml_person_get_type())
#define GML_PERSON(obj)                                                 \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                                   \
                               GML_TYPE_PERSON,                         \
                               GmlPerson))
#define GML_PERSON_CLASS(klass)                                         \
  (G_TYPE_CHECK_CLASS_CAST ((klass),                                    \
                            GML_TYPE_PERSON,                            \
                            GmlPersonClass))
#define GML_IS_PERSON(obj)                                              \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                                   \
                               GML_TYPE_PERSON))
#define GML_IS_PERSON_CLASS(klass)                                      \
  (G_TYPE_CHECK_CLASS_TYPE ((klass),                                    \
                            GML_TYPE_PERSON))
#define GML_PERSON_GET_CLASS(obj)                                       \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                                    \
                              GML_PERSON,                               \
                              GmlPersonClass))

typedef guint64 GmlPersonId;

typedef struct _GmlPerson      GmlPerson;
typedef struct _GmlPersonClass GmlPersonClass;

struct _GmlPersonClass
{
  GObjectClass parent_class;
};

struct _GmlPerson
{
  GObject parent;

  GmlPersonId id;

  GmlConversation *conversation;

  GTimer *use_age;
  unsigned int use_count;
};

GType
gml_person_get_type (void) G_GNUC_CONST;

gboolean
gml_person_id_equal (gconstpointer v1,
                     gconstpointer v2);

guint
gml_person_id_hash (gconstpointer v);

GmlPersonId
gml_person_generate_id (GSocketAddress *address);

gboolean
gml_person_parse_id (const char *string,
                     GmlPersonId *id);

GmlPerson *
gml_person_new (GmlPersonId id,
                GmlConversation *conversation);

void
gml_person_leave_conversation (GmlPerson *person);

void
gml_person_add_use (GmlPerson *person);

void
gml_person_remove_use (GmlPerson *person);

gboolean
gml_person_has_use (GmlPerson *person);

G_END_DECLS

#endif /* __GML_PERSON_H__ */
