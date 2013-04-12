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

#ifndef __VSX_WATCH_PERSON_HANDLER_H__
#define __VSX_WATCH_PERSON_HANDLER_H__

#include <glib-object.h>
#include <gio/gio.h>
#include "vsx-request-handler.h"

G_BEGIN_DECLS

#define VSX_TYPE_WATCH_PERSON_HANDLER           \
  (vsx_watch_person_handler_get_type())
#define VSX_WATCH_PERSON_HANDLER(obj)                           \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                           \
                               VSX_TYPE_WATCH_PERSON_HANDLER,   \
                               VsxWatchPersonHandler))
#define VSX_WATCH_PERSON_HANDLER_CLASS(klass)                   \
  (G_TYPE_CHECK_CLASS_CAST ((klass),                            \
                            VSX_TYPE_WATCH_PERSON_HANDLER,      \
                            VsxWatchPersonHandlerClass))
#define VSX_IS_WATCH_PERSON_HANDLER(obj)                        \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                           \
                               VSX_TYPE_WATCH_PERSON_HANDLER))
#define VSX_IS_WATCH_PERSON_HANDLER_CLASS(klass)                \
  (G_TYPE_CHECK_CLASS_TYPE ((klass),                            \
                            VSX_TYPE_WATCH_PERSON_HANDLER))
#define VSX_WATCH_PERSON_HANDLER_GET_CLASS(obj)                 \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                            \
                              VSX_WATCH_PERSON_HANDLER,         \
                              VsxWatchPersonHandlerClass))

typedef struct _VsxWatchPersonHandler      VsxWatchPersonHandler;
typedef struct _VsxWatchPersonHandlerClass VsxWatchPersonHandlerClass;

struct _VsxWatchPersonHandlerClass
{
  VsxRequestHandlerClass parent_class;
};

struct _VsxWatchPersonHandler
{
  VsxRequestHandler parent;

  VsxResponse *response;
};

GType
vsx_watch_person_handler_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __VSX_WATCH_PERSON_HANDLER_H__ */
