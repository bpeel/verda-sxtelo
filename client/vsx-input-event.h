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

#ifndef VSX_INPUT_EVENT_H
#define VSX_INPUT_EVENT_H

enum vsx_input_event_type {
        VSX_INPUT_EVENT_TYPE_DRAG_START,
        VSX_INPUT_EVENT_TYPE_DRAG,
        VSX_INPUT_EVENT_TYPE_ZOOM_START,
        VSX_INPUT_EVENT_TYPE_ZOOM,
        VSX_INPUT_EVENT_TYPE_CLICK,
};

struct vsx_input_event {
        enum vsx_input_event_type type;

        union {
                struct {
                        int x, y;
                } drag;

                struct {
                        int x0, y0;
                        int x1, y1;
                } zoom;

                struct {
                        int x, y;
                } click;
        };
};

#endif /* VSX_INPUT_EVENT_H */
