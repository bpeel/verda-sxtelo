/*
 * Copyright © 2008 Kristian Høgsberg
 * Copyright © 2013 Neil Roberts
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

/* This file was originally borrowed from the Wayland source code */

#ifndef __VSX_SIGNAL_H__
#define __VSX_SIGNAL_H__

#include <glib.h>

#include "vsx-list.h"

G_BEGIN_DECLS

typedef struct _VsxListener VsxListener;

typedef void
(* VsxNotifyFunc) (VsxListener *listener,
                   void *data);

typedef struct
{
  VsxList listener_list;
} VsxSignal;

struct _VsxListener
{
  VsxList link;
  VsxNotifyFunc notify;
};

static inline void
vsx_signal_init (VsxSignal *signal)
{
  vsx_list_init (&signal->listener_list);
}

static inline void
vsx_signal_add (VsxSignal *signal,
                VsxListener *listener)
{
  vsx_list_insert (signal->listener_list.prev, &listener->link);
}

static inline void
vsx_signal_emit (VsxSignal *signal,
                 void *data)
{
  VsxListener *l, *next;

  vsx_list_for_each_safe (l, next, &signal->listener_list, link)
    l->notify (l, data);
}

G_END_DECLS

#endif /* __VSX_SIGNAL_H__ */
