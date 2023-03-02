/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2021, 2022  Neil Roberts
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

#ifndef VSX_BSP_H
#define VSX_BSP_H

#include <stdbool.h>

struct vsx_bsp;

struct vsx_bsp *
vsx_bsp_new(int width,
            int weight);

bool
vsx_bsp_add(struct vsx_bsp *bsp,
            int width,
            int height,
            int *x_out,
            int *y_out);

void
vsx_bsp_free(struct vsx_bsp *bsp);

#endif /* VSX_BSP_H */
