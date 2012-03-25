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
  "\r\n"
  "fb\r\n"
  "[\"padding\", \"This padding is here because it seems that for some reason "
  "some browsers don't notify Javascript that there is a new chunk of data "
  "until at least 1024 bytes of the response are received. Just think of all "
  "those wasted bytes! It's sad.\"]\r\n"
  "\r\n"
  "fe\r\n"
  "[\"padding\", \"Here's a joke to pass the while this padding is being "
  "downloaded. Why is a giraffe never alone? Because it has a long neck. "
  "It's not very funny. I apologise for that. Why are you reading this anyway? "
  "Don't you have anything better to do?\"]\r\n"
  "\r\n"
  "ee\r\n"
  "[\"padding\", \"Ĉi tiuj plenumiloj estas ĉi tie ĉar ŝajne ial iuj retumiloj "
  "ne informas na Javascript ke nova datumoj alvenis ĝis almenaŭ 1024 bajtoj "
  "da la respondo reciviĝas. Pensu pri tiu malŝparo de bajtoj! Tio estas "
  "tristiga.\"]\r\n"
  "\r\n"
  "112\r\n"
  "[\"padding\", \"Jen ŝerco por pasigi la tempon dum ĉi tiu malŝparo "
  "elŝutas. Kial girafo neniam solas? Ĉar ĝi havas kolegon. Ĝi estas bona "
  "ŝerco ĉu ne? Mi ŝatas ĝin ĉar ĝi ne havas sencon en la angla. Do jen la "
  "fino kaj nun povas komenci la veraj datumoj. Ĝuu!\"]\r\n"
  "\r\n";

static guint8
start[] =
  "1a\r\n"
  "[\"state\", \"in-progress\"]\r\n"
  "\r\n";

static guint8
end[] =
  "13\r\n"
  "[\"state\", \"done\"]\r\n"
  "\r\n"
  "0\r\n"
  "\r\n";

static guint8
header_first_person_start[] =
  "32\r\n"
  "[\"header\", {\"num\": 0, \"id\": \"";
static guint8
header_second_person_start[] =
  "32\r\n"
  "[\"header\", {\"num\": 1, \"id\": \"";
static guint8
header_end[] =
  "\"}]\r\n\r\n";

/* We need at least this much space before we'll consider adding a
   chunk to the buffer. The 8 is the length of 2³²-1 in hexadecimal,
   the first 2 is for the chunk length terminator and the second two
   is for the data terminator */
#define CHUNK_LENGTH_SIZE (8 + 2 + 2)

typedef struct
{
  guint8 *data;
  unsigned int length;
} WriteMessageData;

static gboolean
write_message (GmlWatchPersonResponse *self,
               WriteMessageData *message_data,
               const guint8 *message,
               unsigned int message_length)
{
  unsigned int to_write = MIN (message_data->length,
                               message_length - self->message_pos);

  memcpy (message_data->data, message + self->message_pos, to_write);
  message_data->data += to_write;
  message_data->length -= to_write;
  self->message_pos += to_write;

  return self->message_pos >= message_length;
}

#define write_static_message(self, message_data, message) \
  write_message (self, message_data, message, sizeof (message) - 1)

static gboolean
gml_watch_person_response_get_typing_state (GmlWatchPersonResponse *self)
{
  GmlPerson *person = self->person;
  GmlConversation *conversation = person->conversation;

  return !!(conversation->typing_mask & (1 << (person->person_num ^ 1)));
}

static unsigned int
gml_watch_person_response_add_data (GmlResponse *response,
                                    guint8 *data_in,
                                    unsigned int length_in)
{
  GmlWatchPersonResponse *self = GML_WATCH_PERSON_RESPONSE (response);
  WriteMessageData message_data;

  message_data.data = data_in;
  message_data.length = length_in;

  while (TRUE)
    switch (self->state)
      {
      case GML_WATCH_PERSON_RESPONSE_WRITING_HTTP_HEADER:
        {
          if (write_static_message (self, &message_data, header))
            {
              self->message_pos = 0;
              self->state = GML_WATCH_PERSON_RESPONSE_WRITING_HEADER_START;
            }
          else
            goto done;
        }
        break;

      case GML_WATCH_PERSON_RESPONSE_WRITING_HEADER_START:
        {
          if (self->person->person_num == 0
              ? write_static_message (self, &message_data,
                                      header_first_person_start)
              : write_static_message (self, &message_data,
                                      header_second_person_start))
            {
              self->message_pos = 0;
              self->state = GML_WATCH_PERSON_RESPONSE_WRITING_HEADER_ID;
            }
          else
            goto done;
        }
        break;

      case GML_WATCH_PERSON_RESPONSE_WRITING_HEADER_ID:
        {
          char id_buf[17];

          G_STATIC_ASSERT (sizeof (GmlPersonId) == sizeof (guint64));

          g_snprintf (id_buf, sizeof (id_buf),
                      "%016" G_GINT64_MODIFIER "X",
                      self->person->id);

          if (write_message (self, &message_data, (guint8 *) id_buf, 16))
            {
              self->message_pos = 0;
              self->state = GML_WATCH_PERSON_RESPONSE_WRITING_HEADER_END;
            }
          else
            goto done;
        }
        break;

      case GML_WATCH_PERSON_RESPONSE_WRITING_HEADER_END:
        {
          if (write_static_message (self, &message_data, header_end))
            {
              self->message_pos = 0;
              self->state = GML_WATCH_PERSON_RESPONSE_AWAITING_START;
            }
          else
            goto done;
        }
        break;

      case GML_WATCH_PERSON_RESPONSE_AWAITING_START:
        {
          GmlConversation *conversation = self->person->conversation;

          if (conversation->state == GML_CONVERSATION_AWAITING_PARTNER)
            goto done;

          self->message_pos = 0;
          self->state = GML_WATCH_PERSON_RESPONSE_WRITING_START;
        }
        break;

      case GML_WATCH_PERSON_RESPONSE_WRITING_START:
        if (write_static_message (self, &message_data, start))
          {
            self->message_pos = 0;
            self->state = GML_WATCH_PERSON_RESPONSE_WRITING_MESSAGES;
          }
        else
          goto done;
        break;

      case GML_WATCH_PERSON_RESPONSE_WRITING_MESSAGES:
        {
          GmlConversation *conversation = self->person->conversation;
          GmlConversationMessage *message;
          unsigned int to_write;
          int length_length;

          if (self->message_num >= conversation->messages->len)
            {
              gboolean typing_state;

              if (conversation->state == GML_CONVERSATION_FINISHED)
                {
                  self->message_pos = 0;
                  self->state = GML_WATCH_PERSON_RESPONSE_WRITING_END;
                }
              else if ((typing_state =
                        gml_watch_person_response_get_typing_state (self))
                       != self->last_typing_state)
                {
                  static const guint8 typing_message[] =
                    "c\r\n"
                    "[\"typing\"]\r\n"
                    "\r\n";
                  static const guint8 not_typing_message[] =
                    "10\r\n"
                    "[\"not-typing\"]\r\n"
                    "\r\n";

                  if (typing_state)
                    {
                      /* Only add the typing mask if there's enough
                         space in the buffer for the entire message so
                         that we can't accidentally add it in the
                         middle of sending a message */
                      if (sizeof (typing_message) - 1 <= message_data.length)
                        {
                          write_static_message (self,
                                                &message_data,
                                                typing_message);
                          self->message_pos = 0;
                          self->last_typing_state = TRUE;
                        }
                      else
                        goto done;
                    }
                  else if (sizeof (not_typing_message) - 1
                           <= message_data.length)
                    {
                      write_static_message (self,
                                            &message_data,
                                            not_typing_message);
                      self->message_pos = 0;
                      self->last_typing_state = FALSE;
                    }
                  else
                    goto done;
                }
              else
                goto done;
            }
          else
            {
              /* If there's not enough space left in the buffer to
                 write a large chunk length then we'll wait until the
                 next call to add any data */
              if (message_data.length <= CHUNK_LENGTH_SIZE)
                goto done;

              message = &g_array_index (conversation->messages,
                                        GmlConversationMessage,
                                        self->message_num);

              to_write = MIN (message_data.length - CHUNK_LENGTH_SIZE,
                              message->length - self->message_pos);

              length_length = sprintf ((char *) message_data.data,
                                       "%x\r\n", to_write);

              message_data.length -= length_length;
              message_data.data += length_length;

              memcpy (message_data.data,
                      message->text + self->message_pos, to_write);
              memcpy (message_data.data + to_write, "\r\n", 2);

              message_data.length -= to_write + 2;
              message_data.data += to_write + 2;

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
          if (write_static_message (self, &message_data, end))
            self->state = GML_WATCH_PERSON_RESPONSE_DONE;
          else
            goto done;
        }
        break;

      case GML_WATCH_PERSON_RESPONSE_DONE:
        goto done;
      }

 done:
  return message_data.data - data_in;
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
    case GML_WATCH_PERSON_RESPONSE_WRITING_HTTP_HEADER:
    case GML_WATCH_PERSON_RESPONSE_WRITING_HEADER_START:
    case GML_WATCH_PERSON_RESPONSE_WRITING_HEADER_ID:
    case GML_WATCH_PERSON_RESPONSE_WRITING_HEADER_END:
      return TRUE;

    case GML_WATCH_PERSON_RESPONSE_AWAITING_START:
      return (self->person->conversation->state
              != GML_CONVERSATION_AWAITING_PARTNER);

    case GML_WATCH_PERSON_RESPONSE_WRITING_START:
      return TRUE;

    case GML_WATCH_PERSON_RESPONSE_WRITING_MESSAGES:
      if (self->person->conversation->state == GML_CONVERSATION_FINISHED)
        return TRUE;

      return (self->message_num < self->person->conversation->messages->len
              || (self->last_typing_state
                  != gml_watch_person_response_get_typing_state (self)));

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

  self->person_changed_handler =
    g_signal_connect (person,
                      "changed",
                      G_CALLBACK (person_changed_cb),
                      self);

  return GML_RESPONSE (self);
}
