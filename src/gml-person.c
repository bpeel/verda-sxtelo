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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib-object.h>
#include <string.h>

#include "gml-person.h"
#include "gml-marshal.h"

G_DEFINE_TYPE (GmlPerson, gml_person, G_TYPE_OBJECT);

enum
{
  CHANGED_SIGNAL,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

/* Time in seconds after the last 'use' of the person is removed
   before the person is considered not in use */
#define GML_PERSON_USE_EXPIRY_TIME (60 * 5)

static void
gml_person_dispose (GObject *object)
{
  GmlPerson *person = GML_PERSON (object);

  if (person->conversation)
    {
      g_signal_handler_disconnect (person->conversation,
                                   person->conversation_changed_handler);
      g_object_unref (person->conversation);
      person->conversation = NULL;
    }

  G_OBJECT_CLASS (gml_person_parent_class)->dispose (object);
}

static void
gml_person_finalize (GObject *object)
{
  GmlPerson *person = GML_PERSON (object);

  g_timer_destroy (person->use_age);

  G_OBJECT_CLASS (gml_person_parent_class)->finalize (object);
}

static void
gml_person_class_init (GmlPersonClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = gml_person_dispose;
  object_class->finalize = gml_person_finalize;

  signals[CHANGED_SIGNAL] =
    g_signal_new ("changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_FIRST,
                  0, /* no class method */
                  NULL, /* accumulator */
                  NULL, /* accu_data */
                  gml_marshal_VOID__VOID,
                  G_TYPE_NONE, /* return type */
                  0 /* num arguments */);
}

static void
gml_person_init (GmlPerson *self)
{
}

gboolean
gml_person_id_equal (gconstpointer v1,
                     gconstpointer v2)
{
  return *(const GmlPersonId *) v1 == *(const GmlPersonId *) v2;
}

guint
gml_person_id_hash (gconstpointer v)
{
  G_STATIC_ASSERT (sizeof (GmlPersonId) == sizeof (gint64));

  return g_int64_hash (v);
}

GmlPersonId
gml_person_generate_id (GSocketAddress *address)
{
  GmlPersonId id = 0;
  int i;

  /* Generate enough random numbers to fill the id */
  for (i = 0; i < sizeof (id) / sizeof (guint32); i++)
    id |= (GmlPersonId) g_random_int () << (i * sizeof (guint32) * 8);

  if (address)
    {
      gssize native_size = g_socket_address_get_native_size (address);
      guint8 address_buf[native_size];

      /* XOR the bytes of the connection address so that even if
         someone can work out the sequence of random numbers it's
         still hard to predict what the next id will be */

      if (g_socket_address_to_native (address,
                                      address_buf,
                                      native_size,
                                      NULL /* error */))
        {
          int address_pos = 0;
          guint8 *p = (guint8 *) &id;

          for (i = 0; i < sizeof (id); i++)
            {
              p[i] ^= address_buf[address_pos];
              if (++address_pos >= native_size)
                address_pos = 0;
            }
        }
      else
        g_warning ("g_socket_address_to_native failed");
    }

  return id;
}

gboolean
gml_person_parse_id (const char *string,
                     GmlPersonId *id)
{
  const char *p;

  *id = 0;

  for (p = string; *p; p++)
    if (!g_ascii_isxdigit (*p))
      return FALSE;
    else
      *id = (*id << 4) | g_ascii_xdigit_value (*p);

  return p - string == sizeof (GmlPersonId) * 2;
}

static void
conversation_changed_cb (GmlConversation *conversation,
                         GmlPerson *person)
{
  g_signal_emit (person,
                 signals[CHANGED_SIGNAL],
                 0 /* detail */);
}

GmlPerson *
gml_person_new (GmlPersonId id,
                GmlConversation *conversation)
{
  GmlPerson *person = g_object_new (GML_TYPE_PERSON, NULL);

  person->id = id;
  person->conversation = g_object_ref (conversation);
  person->use_age = g_timer_new ();

  person->conversation_changed_handler
    = g_signal_connect (conversation,
                        "changed",
                        G_CALLBACK (conversation_changed_cb),
                        person);

  return person;
}

void
gml_person_leave_conversation (GmlPerson *person)
{
  gml_conversation_finish (person->conversation);
}

void
gml_person_add_use (GmlPerson *person)
{
  /* This adds a mark to indicate that something is using the person
     (such as it being followed by a GmlListenResponse). If this count
     remains 0 for 5 minutes then the person is a candidate to be
     garbage collected */
  person->use_count++;
}

void
gml_person_remove_use (GmlPerson *person)
{
  g_return_if_fail (person->use_age > 0);

  if (--person->use_count == 0)
    g_timer_start (person->use_age);
}

gboolean
gml_person_has_use (GmlPerson *person)
{
  return (person->use_count > 0
          || (g_timer_elapsed (person->use_age, NULL)
              < GML_PERSON_USE_EXPIRY_TIME));
}
