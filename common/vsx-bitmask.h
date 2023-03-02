/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2021  Neil Roberts
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

#ifndef VSX_BITMASK_H
#define VSX_BITMASK_H

#include <stdbool.h>
#include <limits.h>
#include <string.h>

#include "vsx-buffer.h"

typedef unsigned long vsx_bitmask_element_t;

#define VSX_BITMASK_ELEMENT_MAX ULONG_MAX
#define VSX_BITMASK_ELEMENT_ALL (~0UL)

#define VSX_BITMASK_BITS_PER_ELEMENT (sizeof (vsx_bitmask_element_t) * 8)

#define VSX_BITMASK_N_ELEMENTS_FOR_SIZE(size)           \
        (((size) + VSX_BITMASK_BITS_PER_ELEMENT - 1) /  \
         VSX_BITMASK_BITS_PER_ELEMENT)

static inline int
vsx_bitmask_get_bit(int flag_num)
{
        return flag_num & (VSX_BITMASK_BITS_PER_ELEMENT - 1);
}

static inline int
vsx_bitmask_get_element(int flag_num)
{
        return flag_num / VSX_BITMASK_BITS_PER_ELEMENT;
}

static inline void
vsx_bitmask_set(vsx_bitmask_element_t *elements,
                int flag_num,
                bool value)
{
        if (value) {
                elements[vsx_bitmask_get_element(flag_num)] |=
                        1UL << vsx_bitmask_get_bit(flag_num);
        } else {
                elements[vsx_bitmask_get_element(flag_num)] &=
                        ~(1UL << vsx_bitmask_get_bit(flag_num));
        }
}

static inline void
vsx_bitmask_set_range(vsx_bitmask_element_t *elements,
                      int n_flags)
{
        int element = vsx_bitmask_get_element(n_flags);
        int bit = vsx_bitmask_get_bit(n_flags);

        memset(elements, 0xff, element * sizeof (vsx_bitmask_element_t));

        if (bit > 0) {
                elements[element] |=
                        VSX_BITMASK_ELEMENT_ALL >>
                        (VSX_BITMASK_BITS_PER_ELEMENT - bit);
        }
}

static inline bool
vsx_bitmask_get(const vsx_bitmask_element_t *elements,
                int flag_num)
{
        return (elements[vsx_bitmask_get_element(flag_num)] &
                (1UL << vsx_bitmask_get_bit(flag_num)));
}

void
vsx_bitmask_set_buffer(struct vsx_buffer *buffer,
                       int flag_num,
                       bool value);

static inline bool
vsx_bitmask_get_buffer(struct vsx_buffer *buffer,
                       int flag_num)
{
        int element = vsx_bitmask_get_element(flag_num);

        if (element >= buffer->length / sizeof (vsx_bitmask_element_t))
                return false;

        return vsx_bitmask_get((const vsx_bitmask_element_t *) buffer->data,
                               flag_num);
}

#endif /* VSX_BITMASK_H */
