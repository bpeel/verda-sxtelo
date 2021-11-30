/*
 * Copyright © 2008 Kristian Høgsberg
 * Copyright © 2012 Intel Corporation
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

#ifndef VSX_LIST_H
#define VSX_LIST_H

/**
 * VsxList - linked list
 *
 * The list head is of "VsxList" type, and must be initialized
 * using vsx_list_init().  All entries in the list must be of the same
 * type.  The item type must have a "VsxList" member. This
 * member will be initialized by vsx_list_insert(). There is no need to
 * call vsx_list_init() on the individual item. To query if the list is
 * empty in O(1), use vsx_list_empty().
 *
 * Let's call the list reference "VsxList foo_list", the item type as
 * "item_t", and the item member as "VsxList link". The following code
 *
 * The following code will initialize a list:
 *
 *      vsx_list_init (foo_list);
 *      vsx_list_insert (foo_list, item1);      Pushes item1 at the head
 *      vsx_list_insert (foo_list, item2);      Pushes item2 at the head
 *      vsx_list_insert (item2, item3);         Pushes item3 after item2
 *
 * The list now looks like [item2, item3, item1]
 *
 * Will iterate the list in ascending order:
 *
 *      item_t *item;
 *      vsx_list_for_each(item, foo_list, link) {
 *              Do_something_with_item(item);
 *      }
 */

typedef struct _VsxList VsxList;

struct _VsxList
{
  VsxList *prev;
  VsxList *next;
};

void
vsx_list_init (VsxList *list);

void
vsx_list_insert (VsxList *list,
                 VsxList *elm);

void
vsx_list_remove (VsxList *elm);

int
vsx_list_length (VsxList *list);

int
vsx_list_empty (VsxList *list);

void
vsx_list_insert_list (VsxList *list,
                      VsxList *other);

#ifdef __GNUC__
#define vsx_container_of(ptr, sample, member)                           \
  (__typeof__(sample))((char *)(ptr)    -                               \
                       ((char *)&(sample)->member - (char *)(sample)))
#else
#define vsx_container_of(ptr, sample, member)                   \
  (void *)((char *)(ptr)        -                               \
           ((char *)&(sample)->member - (char *)(sample)))
#endif

#define vsx_list_for_each(pos, head, member)                            \
  for (pos = 0, pos = vsx_container_of((head)->next, pos, member);      \
       &pos->member != (head);                                          \
       pos = vsx_container_of(pos->member.next, pos, member))

#define vsx_list_for_each_safe(pos, tmp, head, member)                  \
  for (pos = 0, tmp = 0,                                                \
         pos = vsx_container_of((head)->next, pos, member),             \
         tmp = vsx_container_of((pos)->member.next, tmp, member);       \
       &pos->member != (head);                                          \
       pos = tmp,                                                       \
         tmp = vsx_container_of(pos->member.next, tmp, member))

#define vsx_list_for_each_reverse(pos, head, member)                    \
  for (pos = 0, pos = vsx_container_of((head)->prev, pos, member);      \
       &pos->member != (head);                                          \
       pos = vsx_container_of(pos->member.prev, pos, member))

#define vsx_list_for_each_reverse_safe(pos, tmp, head, member)          \
  for (pos = 0, tmp = 0,                                                \
         pos = vsx_container_of((head)->prev, pos, member),             \
         tmp = vsx_container_of((pos)->member.prev, tmp, member);       \
       &pos->member != (head);                                          \
       pos = tmp,                                                       \
         tmp = vsx_container_of(pos->member.prev, tmp, member))

#endif /* VSX_LIST_H */
