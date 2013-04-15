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

#ifndef __VSX_SEND_MESSAGE_HANDLER_H__
#define __VSX_SEND_MESSAGE_HANDLER_H__

#include <glib.h>
#include <gio/gio.h>
#include "vsx-request-handler.h"
#include "vsx-person-set.h"
#include "vsx-chunked-iconv.h"

G_BEGIN_DECLS

typedef struct
{
  VsxRequestHandler parent;

  gboolean is_options_request;
  gboolean had_request_method;

  VsxPerson *person;

  VsxResponse *response;

  GIConv data_iconv;

  GString *message_buffer;

  VsxChunkedIconv chunked_iconv;
} VsxSendMessageHandler;

VsxRequestHandler *
vsx_send_message_handler_new (void);

G_END_DECLS

#endif /* __VSX_SEND_MESSAGE_HANDLER_H__ */
