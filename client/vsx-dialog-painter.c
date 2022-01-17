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

#include "config.h"

#include "vsx-dialog-painter.h"

#include <stdbool.h>

#include "vsx-menu-painter.h"
#include "vsx-invite-painter.h"

struct vsx_dialog_painter {
        struct vsx_game_state *game_state;
        struct vsx_listener modified_listener;
        struct vsx_painter_toolbox *toolbox;

        const struct vsx_painter *child_painter;
        void *child_data;
        struct vsx_listener redraw_needed_listener;

        struct vsx_signal redraw_needed_signal;
};

static const struct vsx_painter * const
child_painters[] = {
        [VSX_DIALOG_NONE] = NULL,
        [VSX_DIALOG_MENU] = &vsx_menu_painter,
        [VSX_DIALOG_INVITE_LINK] = &vsx_invite_painter,
};

static void
free_child(struct vsx_dialog_painter *painter)
{
        if (painter->child_painter) {
                if (painter->child_painter->get_redraw_needed_signal_cb)
                        vsx_list_remove(&painter->redraw_needed_listener.link);

                painter->child_painter->free_cb(painter->child_data);

                painter->child_painter = NULL;
                painter->child_data = NULL;
        }
}

static void
update_child(struct vsx_dialog_painter *painter)
{
        const struct vsx_painter *child_painter =
                child_painters[vsx_game_state_get_dialog(painter->game_state)];

        if (child_painter == painter->child_painter)
                return;

        free_child(painter);

        if (child_painter == NULL)
                return;

        painter->child_painter = child_painter;
        painter->child_data =
                child_painter->create_cb(painter->game_state,
                                         painter->toolbox);

        if (child_painter->get_redraw_needed_signal_cb) {
                void *data = painter->child_data;

                struct vsx_signal *signal =
                        child_painter->get_redraw_needed_signal_cb(data);

                vsx_signal_add(signal, &painter->redraw_needed_listener);
        }
}

static void
modified_cb(struct vsx_listener *listener,
            void *user_data)
{
        struct vsx_dialog_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_dialog_painter,
                                 modified_listener);
        const struct vsx_game_state_modified_event *event = user_data;

        switch (event->type) {
        case VSX_GAME_STATE_MODIFIED_TYPE_DIALOG:
                vsx_signal_emit(&painter->redraw_needed_signal, NULL);
                break;
        default:
                break;
        }
}

static void
child_redraw_needed_cb(struct vsx_listener *listener,
                       void *user_data)
{
        struct vsx_dialog_painter *painter =
                vsx_container_of(listener,
                                 struct vsx_dialog_painter,
                                 redraw_needed_listener);

        vsx_signal_emit(&painter->redraw_needed_signal, user_data);
}

static void *
create_cb(struct vsx_game_state *game_state,
          struct vsx_painter_toolbox *toolbox)
{
        struct vsx_dialog_painter *painter = vsx_calloc(sizeof *painter);

        painter->game_state = game_state;
        painter->toolbox = toolbox;

        vsx_signal_init(&painter->redraw_needed_signal);

        painter->redraw_needed_listener.notify = child_redraw_needed_cb;

        painter->modified_listener.notify = modified_cb;
        vsx_signal_add(vsx_game_state_get_modified_signal(game_state),
                       &painter->modified_listener);

        return painter;
}

static void
fb_size_changed_cb(void *painter_data)
{
        struct vsx_dialog_painter *painter = painter_data;

        if (painter->child_painter == NULL ||
            painter->child_painter->fb_size_changed_cb == NULL)
                return;

        painter->child_painter->fb_size_changed_cb(painter->child_data);
}

static void
prepare_cb(void *painter_data)
{
        struct vsx_dialog_painter *painter = painter_data;

        update_child(painter);

        if (painter->child_painter == NULL ||
            painter->child_painter->prepare_cb == NULL)
                return;

        return painter->child_painter->prepare_cb(painter->child_data);
}

static void
paint_cb(void *painter_data)
{
        struct vsx_dialog_painter *painter = painter_data;

        if (painter->child_painter == NULL ||
            painter->child_painter->paint_cb == NULL)
                return;

        return painter->child_painter->paint_cb(painter->child_data);
}

static bool
input_event_cb(void *painter_data,
               const struct vsx_input_event *event)
{
        struct vsx_dialog_painter *painter = painter_data;

        if (painter->child_painter == NULL ||
            painter->child_painter->input_event_cb == NULL)
                return false;

        return painter->child_painter->input_event_cb(painter->child_data,
                                                      event);
}

static struct vsx_signal *
get_redraw_needed_signal_cb(void *painter_data)
{
        struct vsx_dialog_painter *painter = painter_data;

        return &painter->redraw_needed_signal;
}

static void
free_cb(void *painter_data)
{
        struct vsx_dialog_painter *painter = painter_data;

        free_child(painter);

        vsx_list_remove(&painter->modified_listener.link);

        vsx_free(painter);
}

const struct vsx_painter
vsx_dialog_painter = {
        .create_cb = create_cb,
        .fb_size_changed_cb = fb_size_changed_cb,
        .prepare_cb = prepare_cb,
        .paint_cb = paint_cb,
        .input_event_cb = input_event_cb,
        .get_redraw_needed_signal_cb = get_redraw_needed_signal_cb,
        .free_cb = free_cb,
};
