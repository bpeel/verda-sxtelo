/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2022  Neil Roberts
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

#ifndef VSX_SHELL_INTERFACE_H
#define VSX_SHELL_INTERFACE_H

/* This struct defines callback functions used to communicate between
 * the upper layers and the painters.
 */

struct vsx_shell_interface {
        /* Ask the upper layers to share a link */
        void
        (* share_link_cb)(struct vsx_shell_interface *shell,
                          const char *link);
};

#endif /* VSX_SHELL_INTERFACE_H */
