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
#include <stdio.h>

#include "gml-watch-person-response.h"

static void gml_watch_person_response_dispose (GObject *object);

G_DEFINE_TYPE (GmlWatchPersonResponse,
               gml_watch_person_response,
               GML_TYPE_RESPONSE);

static guint8
header[] =
  "HTTP/1.1 200 OK\r\n"
  GML_RESPONSE_COMMON_HEADERS
  GML_RESPONSE_DISABLE_CACHE_HEADERS
  "Content-Type: text/plain; charset=UTF-8\r\n"
  "Transfer-Encoding: chunked\r\n"
  "\r\n";

static guint8
end[] =
  "0\r\n"
  "\r\n";

/* We need at least this much space before we'll consider adding a
   chunk to the buffer. The 10 is the length of 2³² in decimal, the
   first 2 is for the chunk length terminator and the second two is
   for the data terminator */
#define CHUNK_LENGTH_SIZE (10 + 2 + 2)

static unsigned int
gml_watch_person_response_add_data (GmlResponse *response,
                                    guint8 *data,
                                    unsigned int length)
{
  GmlWatchPersonResponse *self = GML_WATCH_PERSON_RESPONSE (response);
  unsigned int added = 0;

  while (TRUE)
    switch (self->state)
      {
      case GML_WATCH_PERSON_RESPONSE_WRITING_HEADER:
        {
          unsigned int to_write
            = MIN (length, sizeof (header) - 1 - self->message_pos);

          memcpy (data, header + self->message_pos, to_write);
          data += to_write;
          length -= to_write;
          added += to_write;
          self->message_pos += to_write;

          if (self->message_pos >= sizeof (header) - 1)
            {
              self->message_pos = 0;
              self->state = GML_WATCH_PERSON_RESPONSE_WRITING_MESSAGES;
            }
          else
            goto done;
        }
        break;

      case GML_WATCH_PERSON_RESPONSE_WRITING_MESSAGES:
        {
          GmlConversation *conversation = self->person->conversation;
          GmlConversationMessage *message;
          unsigned int to_write;
          int length_length;

          /* If there's not enough space left in the buffer to
             write a large chunk length then we'll wait until the
             next call to add any data */
          if (length <= CHUNK_LENGTH_SIZE)
            goto done;

          if (self->message_num >= conversation->messages->len)
            {
              if (conversation->state == GML_CONVERSATION_FINISHED)
                {
                  self->message_pos = 0;
                  self->state = GML_WATCH_PERSON_RESPONSE_WRITING_END;
                }
              else
                goto done;
            }
          else
            {
              message = &g_array_index (conversation->messages,
                                        GmlConversationMessage,
                                        self->message_num);

              to_write = MIN (length - CHUNK_LENGTH_SIZE,
                              message->length - self->message_pos);

              length_length = sprintf ((char *) data, "%u\r\n", to_write);

              length -= length_length;
              data += length_length;
              added += length_length;

              memcpy (data, message->text + self->message_pos, to_write);
              memcpy (data + to_write, "\r\n", 2);

              length -= to_write + 2;
              data += to_write + 2;
              added += to_write + 2;

              self->message_pos += to_write;

              if (self->message_pos >= message->length)
                {
                  self->message_pos = 0;
                  self->message_num++;
                }
            }
        }
        break;

      case GML_WATCH_PERSON_RESPONSE_WRITING_END:
        {
          unsigned int to_write
            = MIN (length, sizeof (end) - 1 - self->message_pos);

          memcpy (data, end + self->message_pos, to_write);
          data += to_write;
          length -= to_write;
          added += to_write;
          self->message_pos += to_write;

          if (self->message_pos >= sizeof (end) - 1)
            self->state = GML_WATCH_PERSON_RESPONSE_DONE;
          else
            goto done;
        }
        break;

      case GML_WATCH_PERSON_RESPONSE_DONE:
        goto done;
      }

 done:
  return added;
}

static gboolean
gml_watch_person_response_is_finished (GmlResponse *response)
{
  GmlWatchPersonResponse *self = GML_WATCH_PERSON_RESPONSE (response);

  return self->state == GML_WATCH_PERSON_RESPONSE_DONE;
}

static gboolean
gml_watch_person_response_has_data (GmlResponse *response)
{
  GmlWatchPersonResponse *self = GML_WATCH_PERSON_RESPONSE (response);

  switch (self->state)
    {
    case GML_WATCH_PERSON_RESPONSE_WRITING_HEADER:
      return TRUE;

    case GML_WATCH_PERSON_RESPONSE_WRITING_MESSAGES:
      if (self->person->conversation->state == GML_CONVERSATION_FINISHED)
        return TRUE;

      return self->message_num < self->person->conversation->messages->len;

    case GML_WATCH_PERSON_RESPONSE_WRITING_END:
      return TRUE;

    case GML_WATCH_PERSON_RESPONSE_DONE:
      return FALSE;
    }

  g_warn_if_reached ();

  return FALSE;
}

static void
gml_watch_person_response_class_init (GmlWatchPersonResponseClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GmlResponseClass *response_class = (GmlResponseClass *) klass;

  gobject_class->dispose = gml_watch_person_response_dispose;

  response_class->add_data = gml_watch_person_response_add_data;
  response_class->is_finished = gml_watch_person_response_is_finished;
  response_class->has_data = gml_watch_person_response_has_data;
}

static void
gml_watch_person_response_init (GmlWatchPersonResponse *self)
{
}

static void
gml_watch_person_response_dispose (GObject *object)
{
  GmlWatchPersonResponse *self = (GmlWatchPersonResponse *) object;

  if (self->person)
    {
      gml_person_remove_use (self->person);
      g_signal_handler_disconnect (self->person,
                                   self->person_changed_handler);
      g_object_unref (self->person);
      self->person = NULL;
    }

  G_OBJECT_CLASS (gml_watch_person_response_parent_class)->dispose (object);
}

static void
person_changed_cb (GmlPerson *person,
                   GmlWatchPersonResponse *response)
{
  gml_response_changed (GML_RESPONSE (response));
}

GmlResponse *
gml_watch_person_response_new (GmlPerson *person)
{
  GmlWatchPersonResponse *self =
    g_object_new (GML_TYPE_WATCH_PERSON_RESPONSE, NULL);

  self->person = g_object_ref (person);

  gml_person_add_use (person);

  self->person_changed_handler =
    g_signal_connect (person,
                      "changed",
                      G_CALLBACK (person_changed_cb),
                      self);

  return GML_RESPONSE (self);
}
