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

#include "config.h"

#include <assert.h>
#include <limits.h>

#include "vsx-bsp.h"

static void
test_exact_size(void)
{
        struct vsx_bsp *bsp = vsx_bsp_new(1024, 1024);
        int x = INT_MAX, y = INT_MAX;

        /* Try adding something too big */
        assert(!vsx_bsp_add(bsp, 1025, 1024, &x, &y));
        assert(!vsx_bsp_add(bsp, 1024, 1025, &x, &y));
        /* Try adding something that has exactly the right size */
        assert(vsx_bsp_add(bsp, 1024, 1024, &x, &y));
        assert(x == 0);
        assert(y == 0);
        /* Nothing else can be added */
        assert(!vsx_bsp_add(bsp, 1, 1, &x, &y));

        vsx_bsp_free(bsp);
}

static void
test_fill_small_squares(void)
{
        struct vsx_bsp *bsp = vsx_bsp_new(1024, 1024);
        int x = INT_MAX, y = INT_MAX;

        for (int j = 0; j < 32; j++) {
                for (int i = 0; i < 32; i++) {
                        assert(vsx_bsp_add(bsp, 32, 32, &x, &y));
                        assert(x == j * 32);
                        assert(y == i * 32);
                }
        }

        /* Nothing else can be added */
        assert(!vsx_bsp_add(bsp, 1, 1, &x, &y));

        vsx_bsp_free(bsp);
}

int
main(int argc, char **argv)
{
        test_exact_size();
        test_fill_small_squares();
}
