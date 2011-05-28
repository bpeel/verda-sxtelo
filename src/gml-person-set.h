/*
 * Gemelo - A person_set for chatting with strangers in a foreign language
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

#include <glib.h>
#include <gio/gio.h>
#include "gml-person.h"

G_BEGIN_DECLS

typedef GHashTable GmlPersonSet;

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
gml_person_set_free (GmlPersonSet *set);

G_END_DECLS

#endif /* __GML_PERSON_SET_H__ */
