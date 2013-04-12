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

#ifndef __VSX_PERSON_H__
#define __VSX_PERSON_H__

#include <glib-object.h>
#include <gio/gio.h>
#include "vsx-conversation.h"

G_BEGIN_DECLS

#define VSX_TYPE_PERSON                                                 \
  (vsx_person_get_type())
#define VSX_PERSON(obj)                                                 \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                                   \
                               VSX_TYPE_PERSON,                         \
                               VsxPerson))
#define VSX_PERSON_CLASS(klass)                                         \
  (G_TYPE_CHECK_CLASS_CAST ((klass),                                    \
                            VSX_TYPE_PERSON,                            \
                            VsxPersonClass))
#define VSX_IS_PERSON(obj)                                              \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                                   \
                               VSX_TYPE_PERSON))
#define VSX_IS_PERSON_CLASS(klass)                                      \
  (G_TYPE_CHECK_CLASS_TYPE ((klass),                                    \
                            VSX_TYPE_PERSON))
#define VSX_PERSON_GET_CLASS(obj)                                       \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                                    \
                              VSX_PERSON,                               \
                              VsxPersonClass))

typedef guint64 VsxPersonId;

typedef struct _VsxPerson      VsxPerson;
typedef struct _VsxPersonClass VsxPersonClass;

struct _VsxPersonClass
{
  GObjectClass parent_class;
};

struct _VsxPerson
{
  GObject parent;

  VsxPersonId id;
  /* This will be zero for one of the people in the conversation and
     one for the other */
  unsigned int person_num;

  char *player_name;

  VsxConversation *conversation;
  guint conversation_changed_handler;

  gint64 last_noise_time;
};

GType
vsx_person_get_type (void) G_GNUC_CONST;

gboolean
vsx_person_id_equal (gconstpointer v1,
                     gconstpointer v2);

guint
vsx_person_id_hash (gconstpointer v);

VsxPersonId
vsx_person_generate_id (GSocketAddress *address);

gboolean
vsx_person_parse_id (const char *string,
                     VsxPersonId *id);

VsxPerson *
vsx_person_new (VsxPersonId id,
                const char *player_name,
                VsxConversation *conversation);

void
vsx_person_make_noise (VsxPerson *person);

gboolean
vsx_person_is_silent (VsxPerson *person);

G_END_DECLS

#endif /* __VSX_PERSON_H__ */
