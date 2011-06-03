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

#ifndef __GML_PERSON_SET_H__
#define __GML_PERSON_SET_H__

#include <glib-object.h>
#include <gio/gio.h>
#include "gml-person.h"

G_BEGIN_DECLS

#define GML_TYPE_PERSON_SET                                             \
  (gml_person_set_get_type())
#define GML_PERSON_SET(obj)                                             \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                                   \
                               GML_TYPE_PERSON_SET,                     \
                               GmlPersonSet))
#define GML_PERSON_SET_CLASS(klass)                                     \
  (G_TYPE_CHECK_CLASS_CAST ((klass),                                    \
                            GML_TYPE_PERSON_SET,                        \
                            GmlPersonSetClass))
#define GML_IS_PERSON_SET(obj)                                          \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                                   \
                               GML_TYPE_PERSON_SET))
#define GML_IS_PERSON_SET_CLASS(klass)                                  \
  (G_TYPE_CHECK_CLASS_TYPE ((klass),                                    \
                            GML_TYPE_PERSON_SET))
#define GML_PERSON_SET_GET_CLASS(obj)                                   \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                                    \
                              GML_PERSON_SET,                           \
                              GmlPersonSetClass))

typedef struct _GmlPersonSet      GmlPersonSet;
typedef struct _GmlPersonSetClass GmlPersonSetClass;

struct _GmlPersonSetClass
{
  GObjectClass parent_class;
};

struct _GmlPersonSet
{
  GObject parent;

  GHashTable *hash_table;
};

GType
gml_person_set_get_type (void) G_GNUC_CONST;

GmlPersonSet *
gml_person_set_new (void);

GmlPerson *
gml_person_set_get_person (GmlPersonSet *set,
                           GmlPersonId id);

GmlPerson *
gml_person_set_generate_person (GmlPersonSet *set,
                                GSocketAddress *address,
                                GmlConversation *conversation);

void
gml_person_set_remove_person (GmlPersonSet *set,
                              GmlPerson *person);

void
gml_person_set_remove_useless_people (GmlPersonSet *set);

G_END_DECLS

#endif /* __GML_PERSON_SET_H__ */
