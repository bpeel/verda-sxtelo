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

#ifndef __GML_MAIN_CONTEXT_H__
#define __GML_MAIN_CONTEXT_H__

#include <glib.h>

G_BEGIN_DECLS

typedef enum
{
  GML_MAIN_CONTEXT_ERROR_UNSUPPORTED,
  GML_MAIN_CONTEXT_ERROR_UNKNOWN
} GmlMainContextError;

typedef enum
{
  GML_MAIN_CONTEXT_POLL_IN = 1 << 0,
  GML_MAIN_CONTEXT_POLL_OUT = 1 << 1,
  GML_MAIN_CONTEXT_POLL_ERROR = 1 << 2,
} GmlMainContextPollFlags;

#define GML_MAIN_CONTEXT_ERROR (gml_main_context_error_quark ())

typedef struct _GmlMainContext GmlMainContext;
typedef struct _GmlMainContextSource GmlMainContextSource;

typedef void (* GmlMainContextPollCallback) (GmlMainContextSource *source,
                                             int fd,
                                             GmlMainContextPollFlags flags,
                                             void *user_data);

typedef void (* GmlMainContextTimerCallback) (GmlMainContextSource *source,
                                              void *user_data);

typedef void (* GmlMainContextQuitCallback) (GmlMainContextSource *source,
                                             void *user_data);

GmlMainContext *
gml_main_context_new (GError **error);

GmlMainContext *
gml_main_context_get_default (GError **error);

GmlMainContextSource *
gml_main_context_add_poll (GmlMainContext *mc,
                           int fd,
                           GmlMainContextPollFlags flags,
                           GmlMainContextPollCallback callback,
                           void *user_data);

void
gml_main_context_modify_poll (GmlMainContextSource *source,
                              GmlMainContextPollFlags flags);

GmlMainContextSource *
gml_main_context_add_timer (GmlMainContext *mc,
                            GmlMainContextTimerCallback callback,
                            void *user_data);

GmlMainContextSource *
gml_main_context_add_quit (GmlMainContext *mc,
                           GmlMainContextQuitCallback callback,
                           void *user_data);

void
gml_main_context_set_timer (GmlMainContextSource *source,
                            unsigned int timeout_msecs);

void
gml_main_context_remove_source (GmlMainContextSource *source);

void
gml_main_context_poll (GmlMainContext *mc,
                       int timeout);

gint64
gml_main_context_get_monotonic_clock (GmlMainContext *mc);

void
gml_main_context_free (GmlMainContext *mc);

GQuark
gml_main_context_error_quark (void);

G_END_DECLS

#endif /* __GML_MAIN_CONTEXT_H__ */
