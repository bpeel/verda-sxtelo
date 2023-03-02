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

#include "config.h"

#include "vsx-bitmask.h"

void
vsx_bitmask_set_buffer(struct vsx_buffer *buffer,
                       int flag_num,
                       bool value)
{
        int element = vsx_bitmask_get_element(flag_num);
        size_t new_length = (element + 1) * sizeof (vsx_bitmask_element_t);
        size_t old_length = buffer->length;

        if (new_length > old_length) {
                if (!value)
                        return;
                vsx_buffer_set_length(buffer, new_length);
                memset(buffer->data + old_length, 0, new_length - old_length);
        }

        vsx_bitmask_set((vsx_bitmask_element_t *) buffer->data,
                        flag_num,
                        value);
}
