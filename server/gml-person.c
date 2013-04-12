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
#include "gml-main-context.h"

G_DEFINE_TYPE (GmlPerson, gml_person, G_TYPE_OBJECT);

enum
{
  CHANGED_SIGNAL,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

/* Time in microseconds after the last request is sent on a person
   before he/she is considered to be silent */
#define GML_PERSON_SILENCE_TIME (60 * 5 * (gint64) 1000000)

static void
gml_person_dispose (GObject *object)
{
  GmlPerson *person = GML_PERSON (object);

  if (person->conversation)
    {
      g_signal_handler_disconnect (person->conversation,
                                   person->conversation_changed_handler);
      gml_conversation_finish (person->conversation);
      g_object_unref (person->conversation);
      person->conversation = NULL;
    }

  G_OBJECT_CLASS (gml_person_parent_class)->dispose (object);
}

static void
gml_person_finalize (GObject *object)
{
  GmlPerson *person = GML_PERSON (object);

  g_free (person->player_name);

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
  gml_person_make_noise (self);
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
                const char *player_name,
                GmlConversation *conversation)
{
  GmlPerson *person = g_object_new (GML_TYPE_PERSON, NULL);

  person->id = id;
  person->conversation = g_object_ref (conversation);
  person->player_name = g_strdup (player_name);

  if (conversation->state == GML_CONVERSATION_AWAITING_PARTNER)
    person->person_num = 0;
  else
    person->person_num = 1;

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
gml_person_make_noise (GmlPerson *person)
{
  person->last_noise_time = gml_main_context_get_monotonic_clock (NULL);
}

gboolean
gml_person_is_silent (GmlPerson *person)
{
  return ((gml_main_context_get_monotonic_clock (NULL)
           - person->last_noise_time)
          > GML_PERSON_SILENCE_TIME);
}
