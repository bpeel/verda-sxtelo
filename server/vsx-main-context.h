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

#ifndef __VSX_MAIN_CONTEXT_H__
#define __VSX_MAIN_CONTEXT_H__

#include <glib.h>

G_BEGIN_DECLS

typedef enum
{
  VSX_MAIN_CONTEXT_ERROR_UNSUPPORTED,
  VSX_MAIN_CONTEXT_ERROR_UNKNOWN
} VsxMainContextError;

typedef enum
{
  VSX_MAIN_CONTEXT_POLL_IN = 1 << 0,
  VSX_MAIN_CONTEXT_POLL_OUT = 1 << 1,
  VSX_MAIN_CONTEXT_POLL_ERROR = 1 << 2,
} VsxMainContextPollFlags;

#define VSX_MAIN_CONTEXT_ERROR (vsx_main_context_error_quark ())

typedef struct _VsxMainContext VsxMainContext;
typedef struct _VsxMainContextSource VsxMainContextSource;

typedef void (* VsxMainContextPollCallback) (VsxMainContextSource *source,
                                             int fd,
                                             VsxMainContextPollFlags flags,
                                             void *user_data);

typedef void (* VsxMainContextTimerCallback) (VsxMainContextSource *source,
                                              void *user_data);

typedef void (* VsxMainContextQuitCallback) (VsxMainContextSource *source,
                                             void *user_data);

VsxMainContext *
vsx_main_context_new (GError **error);

VsxMainContext *
vsx_main_context_get_default (GError **error);

VsxMainContextSource *
vsx_main_context_add_poll (VsxMainContext *mc,
                           int fd,
                           VsxMainContextPollFlags flags,
                           VsxMainContextPollCallback callback,
                           void *user_data);

void
vsx_main_context_modify_poll (VsxMainContextSource *source,
                              VsxMainContextPollFlags flags);

VsxMainContextSource *
vsx_main_context_add_quit (VsxMainContext *mc,
                           VsxMainContextQuitCallback callback,
                           void *user_data);

VsxMainContextSource *
vsx_main_context_add_timer (VsxMainContext *mc,
                            int minutes,
                            VsxMainContextTimerCallback callback,
                            void *user_data);

void
vsx_main_context_remove_source (VsxMainContextSource *source);

void
vsx_main_context_poll (VsxMainContext *mc);

int64_t
vsx_main_context_get_monotonic_clock (VsxMainContext *mc);

void
vsx_main_context_free (VsxMainContext *mc);

GQuark
vsx_main_context_error_quark (void);

G_END_DECLS

#endif /* __VSX_MAIN_CONTEXT_H__ */
