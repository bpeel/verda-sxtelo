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

#include "vsx-signal.h"

/* This struct defines callback functions used to communicate between
 * the upper layers and the painters.
 */

struct vsx_shell_interface {
        /* Queue a redraw of the entire interface */
        void
        (* queue_redraw_cb)(struct vsx_shell_interface *shell);

        /* Ask the shell to log an error somewhere */
        VSX_PRINTF_FORMAT(2, 3)
        void
        (* log_error_cb)(struct vsx_shell_interface *shell,
                         const char *format,
                         ...);

        /* Ask the upper layers to share a link. The link rectangle is
         * the rectangle where the link is drawn. The shell can
         * optionally use this information to draw a popup in the
         * right place.
         */
        void
        (* share_link_cb)(struct vsx_shell_interface *shell,
                          const char *link,
                          int link_x, int link_y,
                          int link_width, int link_height);

        /* Tell the upper layers about the y-position that we want the
         * name entry to appear at.
         */
        void
        (* set_name_position_cb)(struct vsx_shell_interface *shell,
                                 int y_pos,
                                 int max_width);

        /* Ask the upper layers what size it has chosen for the name
         * entry box.
         */
        int
        (* get_name_height_cb)(struct vsx_shell_interface *shell);

        /* Request that the upper layers set the name that the user
         * has typed in. This is called in response to the user
         * clicking the join/new game button.
         */
        void
        (* request_name_cb)(struct vsx_shell_interface *shell);

        /* Signal emitted by the upper layers to inform that a layout
         * has occured and the final size of the name entry has been
         * chosen.
         */
        struct vsx_signal name_size_signal;
};

#endif /* VSX_SHELL_INTERFACE_H */
