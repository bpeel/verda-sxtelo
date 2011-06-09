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

#include "gml-send-message-handler.h"
#include "gml-string-response.h"
#include "gml-parse-content-type.h"

G_DEFINE_TYPE (GmlSendMessageHandler,
               gml_send_message_handler,
               GML_TYPE_REQUEST_HANDLER);

static void
real_dispose (GObject *object)
{
  GmlSendMessageHandler *handler = GML_SEND_MESSAGE_HANDLER (object);

  if (handler->person)
    {
      g_object_unref (handler->person);
      handler->person = NULL;
    }

  if (handler->response)
    {
      g_object_unref (handler->response);
      handler->response = NULL;
    }

  G_OBJECT_CLASS (gml_send_message_handler_parent_class)->dispose (object);
}

static void
real_finalize (GObject *object)
{
  GmlSendMessageHandler *handler = GML_SEND_MESSAGE_HANDLER (object);

  if (handler->data_iconv != (GIConv) -1)
    g_iconv_close (handler->data_iconv);

  if (handler->message_buffer)
    g_string_free (handler->message_buffer, TRUE);

  G_OBJECT_CLASS (gml_send_message_handler_parent_class)->finalize (object);
}

static void
set_error (GmlSendMessageHandler *self,
           GmlStringResponseType type)
{
  if (self->person)
    {
      g_object_unref (self->person);
      self->person = NULL;
    }

  if (self->data_iconv != (GIConv) -1)
    {
      g_iconv_close (self->data_iconv);
      self->data_iconv = (GIConv) -1;
    }

  if (self->response == NULL)
    self->response = gml_string_response_new (type);
}

static void
real_request_line_received (GmlRequestHandler *handler,
                            GmlRequestMethod method,
                            const char *query_string)
{
  GmlSendMessageHandler *self = GML_SEND_MESSAGE_HANDLER (handler);
  GmlPersonId id;

  if (method == GML_REQUEST_METHOD_POST
      && gml_person_parse_id (query_string, &id))
    {
      self->person = gml_person_set_get_person (handler->person_set, id);

      if (self->person == NULL)
        set_error (self, GML_STRING_RESPONSE_NOT_FOUND);
      else
        g_object_ref (self->person);
    }
  else
    set_error (self, GML_STRING_RESPONSE_BAD_REQUEST);
}

static gboolean
handle_content_type_cb (const char *content_type,
                        void *user_data)
{
  GmlSendMessageHandler *self = user_data;

  /* The content must be text/plain */
  if (g_ascii_strcasecmp ("text/plain", content_type))
    {
      set_error (self, GML_STRING_RESPONSE_UNSUPPORTED_REQUEST);
      return FALSE;
    }

  return TRUE;
}

static gboolean
handle_parameter_cb (const char *name,
                     const char *value,
                     void *user_data)
{
  GmlSendMessageHandler *self = user_data;

  if (!g_ascii_strcasecmp ("charset", name))
    {
      /* If the client specifies the charset twice then it's gone wrong */
      if (self->data_iconv != (GIConv) -1)
        {
          set_error (self, GML_STRING_RESPONSE_BAD_REQUEST);
          return FALSE;
        }
      else if ((self->data_iconv = g_iconv_open ("UTF-8", value))
               == (GIConv) -1)
        {
          set_error (self, GML_STRING_RESPONSE_UNSUPPORTED_REQUEST);
          return FALSE;
        }
      else
        gml_chunked_iconv_init (&self->chunked_iconv,
                                self->data_iconv,
                                self->message_buffer);
    }

  return TRUE;
}

static void
real_header_received (GmlRequestHandler *handler,
                      const char *field_name,
                      const char *value)
{
  GmlSendMessageHandler *self = GML_SEND_MESSAGE_HANDLER (handler);

  /* Ignore the header if we've already encountered some error */
  if (self->person)
    {
      if (!g_ascii_strcasecmp (field_name, "content-type"))
        {
          /* If we get the content-type header a second time then
             it's an error */
          if (self->data_iconv != (GIConv) -1)
            set_error (self, GML_STRING_RESPONSE_BAD_REQUEST);
          else if (gml_parse_content_type (value,
                                           handle_content_type_cb,
                                           handle_parameter_cb,
                                           handler))
            {
              /* If we didn't get a charset then we'll assume
                 ISO-8859-1 */
              if (self->data_iconv == (GIConv) -1)
                {
                  self->data_iconv = g_iconv_open ("UTF-8", "ISO-8859-1");

                  if (self->data_iconv == (GIConv) -1)
                    set_error (self, GML_STRING_RESPONSE_UNSUPPORTED_REQUEST);
                  else
                    gml_chunked_iconv_init (&self->chunked_iconv,
                                            self->data_iconv,
                                            self->message_buffer);
                }
            }
          else
            set_error (self, GML_STRING_RESPONSE_BAD_REQUEST);
        }
    }
}

static void
real_data_received (GmlRequestHandler *handler,
                    const guint8 *data,
                    unsigned int length)
{
  GmlSendMessageHandler *self = GML_SEND_MESSAGE_HANDLER (handler);

  /* Ignore the header if we've already encountered some error */
  if (self->person)
    {
      /* If we haven't got a GIConv then that must mean we didn't see
         the content-type header so let's assume that's an error */
      if (self->data_iconv == (GIConv) -1)
        {
          set_error (self, GML_STRING_RESPONSE_BAD_REQUEST);
          return;
        }

      if (!gml_chunked_iconv_add_data (&self->chunked_iconv,
                                       data,
                                       length))
        {
          set_error (self, GML_STRING_RESPONSE_BAD_REQUEST);
          return;
        }
    }
}

static GmlResponse *
real_request_finished (GmlRequestHandler *handler)
{
  GmlSendMessageHandler *self = GML_SEND_MESSAGE_HANDLER (handler);

  if (self->person)
    {
      if (self->data_iconv == (GIConv) -1
          || !gml_chunked_iconv_eos (&self->chunked_iconv)
          || self->person->conversation == NULL)
        return gml_string_response_new (GML_STRING_RESPONSE_BAD_REQUEST);

      gml_conversation_add_message (self->person->conversation,
                                    self->person->person_num,
                                    self->message_buffer->str,
                                    self->message_buffer->len);

      g_string_free (self->message_buffer, TRUE);
      self->message_buffer = NULL;

      return gml_string_response_new (GML_STRING_RESPONSE_OK);
    }
  else if (self->response)
    return g_object_ref (self->response);
  else
    {
      g_warn_if_reached ();

      return gml_string_response_new (GML_STRING_RESPONSE_BAD_REQUEST);
    }
}

static void
gml_send_message_handler_class_init (GmlSendMessageHandlerClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  GmlRequestHandlerClass *request_handler_class
    = (GmlRequestHandlerClass *) klass;

  object_class->dispose = real_dispose;
  object_class->finalize = real_finalize;

  request_handler_class->request_line_received = real_request_line_received;
  request_handler_class->header_received = real_header_received;
  request_handler_class->data_received = real_data_received;
  request_handler_class->request_finished = real_request_finished;
}

static void
gml_send_message_handler_init (GmlSendMessageHandler *self)
{
  self->data_iconv = (GIConv) -1;

  self->message_buffer = g_string_new (NULL);
}