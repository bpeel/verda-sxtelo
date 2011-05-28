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

#include <string.h>
#include "gml-new-person-response.h"

static void gml_new_person_response_dispose (GObject *object);

G_DEFINE_TYPE (GmlNewPersonResponse,
               gml_new_person_response,
               GML_TYPE_RESPONSE);

static guint8
header[] =
  "HTTP/1.1 200 OK\r\n"
  GML_RESPONSE_COMMON_HEADERS
  GML_RESPONSE_DISABLE_CACHE_HEADERS
  "Content-Type: text/plain; charset=ISO-8859-1\r\n"
  "Content-Length: 16\r\n"
  "\r\n";

static unsigned int
gml_new_person_response_add_data (GmlResponse *response,
                                  guint8 *data,
                                  unsigned int length)
{
  GmlNewPersonResponse *self = GML_NEW_PERSON_RESPONSE (response);
  unsigned int wrote = 0;

  while (length - wrote > 0)
    switch (self->state)
      {
      case GML_NEW_PERSON_RESPONSE_HEADERS:
        {
          unsigned int to_write = MIN (length - wrote,
                                       sizeof (header) - 1
                                       - self->output_pos);

          memcpy (data + wrote,
                  header + self->output_pos,
                  to_write);

          wrote += to_write;
          self->output_pos += to_write;

          if (self->output_pos >= sizeof (header) - 1)
            {
              self->state = GML_NEW_PERSON_RESPONSE_BODY;
              self->output_pos = 0;
            }
        }
        break;

      case GML_NEW_PERSON_RESPONSE_BODY:
        {
          unsigned int to_write = MIN (length - wrote, 16 - self->output_pos);
          char id_buf[17];

          G_STATIC_ASSERT (sizeof (GmlPersonId) == sizeof (guint64));

          g_snprintf (id_buf, sizeof (id_buf),
                      "%016" G_GINT64_MODIFIER "X",
                      self->person->id);

          memcpy (data + wrote,
                  id_buf + self->output_pos,
                  to_write);

          wrote += to_write;
          self->output_pos += to_write;

          if (self->output_pos >= 16)
            self->state = GML_NEW_PERSON_RESPONSE_DONE;
        }
        break;

      case GML_NEW_PERSON_RESPONSE_DONE:
        goto done;
      }

 done:
  return wrote;
}

static gboolean
gml_new_person_response_is_finished (GmlResponse *response)
{
  GmlNewPersonResponse *self = GML_NEW_PERSON_RESPONSE (response);

  return self->state == GML_NEW_PERSON_RESPONSE_DONE;
}

static void
gml_new_person_response_class_init (GmlNewPersonResponseClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GmlResponseClass *response_class = (GmlResponseClass *) klass;

  gobject_class->dispose = gml_new_person_response_dispose;

  response_class->add_data = gml_new_person_response_add_data;
  response_class->is_finished = gml_new_person_response_is_finished;
}

static void
gml_new_person_response_init (GmlNewPersonResponse *self)
{
}

static void
gml_new_person_response_dispose (GObject *object)
{
  GmlNewPersonResponse *self = (GmlNewPersonResponse *) object;

  if (self->person)
    {
      g_object_unref (self->person);
      self->person = NULL;
    }

  G_OBJECT_CLASS (gml_new_person_response_parent_class)->dispose (object);
}

GmlResponse *
gml_new_person_response_new (GmlPerson *person)
{
  GmlNewPersonResponse *self =
    g_object_new (GML_TYPE_NEW_PERSON_RESPONSE, NULL);

  self->person = g_object_ref (person);

  return GML_RESPONSE (self);
}
