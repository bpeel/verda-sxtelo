/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2011, 2013  Neil Roberts
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

#include <glib.h>
#include <string.h>

#include "vsx-send-message-handler.h"
#include "vsx-string-response.h"
#include "vsx-parse-content-type.h"
#include "vsx-arguments.h"

static void
real_free (void *object)
{
  VsxSendMessageHandler *handler = (VsxSendMessageHandler *) object;

  if (handler->person)
    vsx_object_unref (handler->person);

  if (handler->response)
    vsx_object_unref (handler->response);

  if (handler->data_iconv != (GIConv) -1)
    g_iconv_close (handler->data_iconv);

  if (handler->message_buffer)
    g_string_free (handler->message_buffer, TRUE);

  vsx_request_handler_get_class ()->parent_class.free (object);
}

static void
set_error (VsxSendMessageHandler *self,
           VsxStringResponseType type)
{
  if (self->person)
    {
      vsx_object_unref (self->person);
      self->person = NULL;
    }

  if (self->data_iconv != (GIConv) -1)
    {
      g_iconv_close (self->data_iconv);
      self->data_iconv = (GIConv) -1;
    }

  if (self->response == NULL)
    self->response = vsx_string_response_new (type);
}

static void
real_request_line_received (VsxRequestHandler *handler,
                            VsxRequestMethod method,
                            const char *query_string)
{
  VsxSendMessageHandler *self = (VsxSendMessageHandler *) handler;
  VsxPersonId id;

  if ((method == VSX_REQUEST_METHOD_POST
       || method == VSX_REQUEST_METHOD_OPTIONS)
      && vsx_arguments_parse ("p", query_string, &id))
    {
      VsxPerson *person;

      person = vsx_person_set_activate_person (handler->person_set, id);

      if (person == NULL)
        set_error (self, VSX_STRING_RESPONSE_NOT_FOUND);
      else if (method == VSX_REQUEST_METHOD_OPTIONS)
        self->is_options_request = TRUE;
      else
        self->person = vsx_object_ref (person);
    }
  else
    set_error (self, VSX_STRING_RESPONSE_BAD_REQUEST);
}

static gboolean
handle_content_type_cb (const char *content_type,
                        void *user_data)
{
  VsxSendMessageHandler *self = user_data;

  /* The content must be text/plain */
  if (g_ascii_strcasecmp ("text/plain", content_type))
    {
      set_error (self, VSX_STRING_RESPONSE_UNSUPPORTED_REQUEST);
      return FALSE;
    }

  return TRUE;
}

static gboolean
handle_parameter_cb (const char *name,
                     const char *value,
                     void *user_data)
{
  VsxSendMessageHandler *self = user_data;

  if (!g_ascii_strcasecmp ("charset", name))
    {
      /* If the client specifies the charset twice then it's gone wrong */
      if (self->data_iconv != (GIConv) -1)
        {
          set_error (self, VSX_STRING_RESPONSE_BAD_REQUEST);
          return FALSE;
        }
      else if ((self->data_iconv = g_iconv_open ("UTF-8", value))
               == (GIConv) -1)
        {
          set_error (self, VSX_STRING_RESPONSE_UNSUPPORTED_REQUEST);
          return FALSE;
        }
      else
        vsx_chunked_iconv_init (&self->chunked_iconv,
                                self->data_iconv,
                                self->message_buffer);
    }

  return TRUE;
}

static void
real_header_received (VsxRequestHandler *handler,
                      const char *field_name,
                      const char *value)
{
  VsxSendMessageHandler *self = (VsxSendMessageHandler *) handler;

  /* Ignore the header if we've already encountered some error */
  if (self->response == NULL)
    {
      if (!g_ascii_strcasecmp (field_name, "content-type"))
        {
          /* If we get the content-type header a second time then
             it's an error */
          if (self->data_iconv != (GIConv) -1)
            set_error (self, VSX_STRING_RESPONSE_BAD_REQUEST);
          else if (vsx_parse_content_type (value,
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
                    set_error (self, VSX_STRING_RESPONSE_UNSUPPORTED_REQUEST);
                  else
                    vsx_chunked_iconv_init (&self->chunked_iconv,
                                            self->data_iconv,
                                            self->message_buffer);
                }
            }
          else
            set_error (self, VSX_STRING_RESPONSE_BAD_REQUEST);
        }
      else if (!g_ascii_strcasecmp (field_name,
                                    "Access-Control-Request-Method"))
        {
          if (!self->is_options_request
              || self->had_request_method
              || strcmp (value, "POST"))
            set_error (self, VSX_STRING_RESPONSE_UNSUPPORTED_REQUEST);
          else
            self->had_request_method = TRUE;
        }
    }
}

static void
real_data_received (VsxRequestHandler *handler,
                    const guint8 *data,
                    unsigned int length)
{
  VsxSendMessageHandler *self = (VsxSendMessageHandler *) handler;

  /* Ignore the data if we've already encountered some error */
  if (self->person)
    {
      /* If we haven't got a GIConv then that must mean we didn't see
         the content-type header. In this case we'll try to parse the
         data as text/plain in UTF-8 and hope for the best. This is
         necessary because when using XDomainRequest on Internet
         Exploiter it's not possible to set the content-type header or
         control the charset it sends */
      if (self->data_iconv == (GIConv) -1)
        {
          self->data_iconv = g_iconv_open ("UTF-8", "UTF-8");

          if (self->data_iconv == (GIConv) -1)
            {
              set_error (self, VSX_STRING_RESPONSE_UNSUPPORTED_REQUEST);
              return;
            }

          vsx_chunked_iconv_init (&self->chunked_iconv,
                                  self->data_iconv,
                                  self->message_buffer);
        }

      if (!vsx_chunked_iconv_add_data (&self->chunked_iconv,
                                       data,
                                       length))
        {
          set_error (self, VSX_STRING_RESPONSE_BAD_REQUEST);
          return;
        }
    }
}

static VsxResponse *
real_request_finished (VsxRequestHandler *handler)
{
  VsxSendMessageHandler *self = (VsxSendMessageHandler *) handler;

  if (self->response)
    return vsx_object_ref (self->response);
  else if (self->is_options_request)
    {
      if (self->had_request_method)
        return vsx_string_response_new (VSX_STRING_RESPONSE_PREFLIGHT_POST_OK);
      else
        return vsx_string_response_new (VSX_STRING_RESPONSE_BAD_REQUEST);
    }
  else if (self->person)
    {
      if (self->data_iconv == (GIConv) -1
          || !vsx_chunked_iconv_eos (&self->chunked_iconv)
          || self->person->conversation == NULL)
        return vsx_string_response_new (VSX_STRING_RESPONSE_BAD_REQUEST);

      vsx_conversation_add_message (self->person->conversation,
                                    self->person->player->num,
                                    self->message_buffer->str,
                                    self->message_buffer->len);
      /* Sending a message implicitly marks the person as no longer
         typing */
      vsx_conversation_set_typing (self->person->conversation,
                                   self->person->player->num,
                                   FALSE);

      g_string_free (self->message_buffer, TRUE);
      self->message_buffer = NULL;

      return vsx_string_response_new (VSX_STRING_RESPONSE_OK);
    }
  else
    {
      g_warn_if_reached ();

      return vsx_string_response_new (VSX_STRING_RESPONSE_BAD_REQUEST);
    }
}

static const VsxRequestHandlerClass *
vsx_send_message_handler_get_class (void)
{
  static VsxRequestHandlerClass klass;

  if (klass.parent_class.free == NULL)
    {
      klass = *vsx_request_handler_get_class ();
      klass.parent_class.instance_size = sizeof (VsxSendMessageHandler);
      klass.parent_class.free = real_free;

      klass.request_line_received = real_request_line_received;
      klass.header_received = real_header_received;
      klass.data_received = real_data_received;
      klass.request_finished = real_request_finished;
    }

  return &klass;
}

VsxRequestHandler *
vsx_send_message_handler_new (void)
{
  VsxSendMessageHandler *handler =
    vsx_object_allocate (vsx_send_message_handler_get_class ());

  vsx_request_handler_init (handler);

  handler->data_iconv = (GIConv) -1;

  handler->message_buffer = g_string_new (NULL);

  return (VsxRequestHandler *) handler;
}
