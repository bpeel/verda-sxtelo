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

#include "vsx-bsp.h"

#include <stdint.h>

#include "vsx-util.h"
#include "vsx-buffer.h"

/* A binary-space partitioning data structure to split a 2D region
 * into smaller regions.
 *
 * See this: https://blackpawn.com/texts/lightmaps/default.html
 */

enum vsx_bsp_split_type {
        VSX_BSP_SPLIT_TYPE_TOP_BOTTOM,
        VSX_BSP_SPLIT_TYPE_LEFT_RIGHT,
};

struct vsx_bsp_node {
        enum vsx_bsp_split_type type;
        int split_point;
        uint16_t child_a, child_b;
};

struct vsx_bsp {
        int width, height;
        struct vsx_buffer nodes;
        /* Stack used just for walking the tree */
        struct vsx_buffer stack;
};

struct vsx_bsp_stack_entry {
        int x, y;
        int width, height;
        uint16_t node;
        bool tried_a;
};

#define VSX_BSP_LEAF_FULL UINT16_MAX
#define VSX_BSP_LEAF_EMPTY (UINT16_MAX - 1)

static struct vsx_bsp_node *
add_node(struct vsx_bsp *bsp,
         enum vsx_bsp_split_type type,
         int split_point)
{
        vsx_buffer_set_length(&bsp->nodes,
                              bsp->nodes.length + sizeof (struct vsx_bsp_node));

        struct vsx_bsp_node *node =
                ((struct vsx_bsp_node *)
                 (bsp->nodes.data + bsp->nodes.length) - 1);

        node->type = type;
        node->split_point = split_point;
        node->child_a = VSX_BSP_LEAF_EMPTY;
        node->child_b = VSX_BSP_LEAF_EMPTY;

        return node;
}

static struct vsx_bsp_stack_entry *
get_stack_top(struct vsx_bsp *bsp)
{
        return ((struct vsx_bsp_stack_entry *)
                (bsp->stack.data + bsp->stack.length) - 1);
}

static struct vsx_bsp_stack_entry *
add_stack_entry(struct vsx_bsp *bsp)
{
        vsx_buffer_set_length(&bsp->stack,
                              bsp->stack.length +
                              sizeof (struct vsx_bsp_stack_entry));

        return get_stack_top(bsp);
}

struct vsx_bsp *
vsx_bsp_new(int width, int height)
{
        struct vsx_bsp *bsp = vsx_alloc(sizeof *bsp);

        bsp->width = width;
        bsp->height = height;

        vsx_buffer_init(&bsp->nodes);
        vsx_buffer_init(&bsp->stack);

        add_node(bsp, VSX_BSP_SPLIT_TYPE_TOP_BOTTOM, height);

        return bsp;
}

static void
add_split(struct vsx_bsp *bsp,
          uint16_t *next_node,
          int width,
          int height,
          int add_width,
          int add_height)
{
        int n_nodes = bsp->nodes.length / sizeof (struct vsx_bsp_node);

        if (add_width < width) {
                *next_node = n_nodes++;

                struct vsx_bsp_node *node =
                        add_node(bsp,
                                 VSX_BSP_SPLIT_TYPE_LEFT_RIGHT,
                                 add_width);

                next_node = &node->child_a;
        }

        if (add_height < height) {
                *next_node = n_nodes++;

                struct vsx_bsp_node *node =
                        add_node(bsp,
                                 VSX_BSP_SPLIT_TYPE_TOP_BOTTOM,
                                 add_height);

                next_node = &node->child_a;
        }

        *next_node = VSX_BSP_LEAF_FULL;
}

bool
vsx_bsp_add(struct vsx_bsp *bsp,
            int add_width,
            int add_height,
            int *x_out,
            int *y_out)
{
        vsx_buffer_set_length(&bsp->stack, 0);

        struct vsx_bsp_stack_entry *entry = add_stack_entry(bsp);
        struct vsx_bsp_node *nodes = (struct vsx_bsp_node *) bsp->nodes.data;

        entry->x = 0;
        entry->y = 0;
        entry->width = bsp->width;
        entry->height = bsp->height;
        entry->node = 0;
        entry->tried_a = false;

        while (bsp->stack.length > 0) {
                entry = get_stack_top(bsp);

                struct vsx_bsp_node *node = nodes + entry->node;

                int x = entry->x, y = entry->y;
                int width = entry->width, height = entry->height;
                bool tried_a = entry->tried_a;

                switch (node->type) {
                case VSX_BSP_SPLIT_TYPE_TOP_BOTTOM:
                        if (tried_a) {
                                y += node->split_point;
                                height -= node->split_point;
                        } else {
                                height = node->split_point;
                        }
                        break;

                case VSX_BSP_SPLIT_TYPE_LEFT_RIGHT:
                        if (tried_a) {
                                x += node->split_point;
                                width -= node->split_point;
                        } else {
                                width = node->split_point;
                        }
                        break;
                }

                uint16_t *next_node;

                if (entry->tried_a) {
                        next_node = &node->child_b;
                        size_t new_length =
                                bsp->stack.length -
                                sizeof (struct vsx_bsp_stack_entry);
                        vsx_buffer_set_length(&bsp->stack, new_length);
                } else {
                        next_node = &node->child_a;
                        entry->tried_a = true;
                }

                if (width < add_width || height < add_height)
                        continue;

                switch (*next_node) {
                case VSX_BSP_LEAF_FULL:
                        break;
                case VSX_BSP_LEAF_EMPTY:
                        add_split(bsp,
                                  next_node,
                                  width, height,
                                  add_width, add_height);
                        *x_out = x;
                        *y_out = y;
                        return true;
                default:
                        entry = add_stack_entry(bsp);
                        entry->x = x;
                        entry->y = y;
                        entry->width = width;
                        entry->height = height;
                        entry->node = *next_node;
                        entry->tried_a = false;
                        break;
                }
        }

        return false;
}

void
vsx_bsp_free(struct vsx_bsp *bsp)
{
        vsx_buffer_destroy(&bsp->nodes);
        vsx_buffer_destroy(&bsp->stack);
        vsx_free(bsp);
}
