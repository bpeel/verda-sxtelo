/*
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2011, 2012 Intel Corporation
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

/* This list implementation is based on the Wayland source code */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "vsx-list.h"

void
vsx_list_init (VsxList *list)
{
  list->prev = list;
  list->next = list;
}

void
vsx_list_insert (VsxList *list, VsxList *elm)
{
  elm->prev = list;
  elm->next = list->next;
  list->next = elm;
  elm->next->prev = elm;
}

void
vsx_list_remove (VsxList *elm)
{
  elm->prev->next = elm->next;
  elm->next->prev = elm->prev;
  elm->next = NULL;
  elm->prev = NULL;
}

int
vsx_list_length (VsxList *list)
{
  VsxList *e;
  int count;

  count = 0;
  e = list->next;
  while (e != list)
    {
      e = e->next;
      count++;
    }

  return count;
}

int
vsx_list_empty (VsxList *list)
{
  return list->next == list;
}

void
vsx_list_insert_list (VsxList *list,
                      VsxList *other)
{
  if (vsx_list_empty (other))
    return;

  other->next->prev = list;
  other->prev->next = list->next;
  list->next->prev = other->prev;
  list->next = other->next;
}
