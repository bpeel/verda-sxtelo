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

#include "vsx-game-painter.h"

#include <stdbool.h>
#include <math.h>
#include <string.h>

#include "vsx-painter.h"
#include "vsx-board-painter.h"
#include "vsx-tile-painter.h"
#include "vsx-button-painter.h"
#include "vsx-dialog-painter.h"
#include "vsx-note-painter.h"
#include "vsx-gl.h"
#include "vsx-board.h"

static const struct vsx_painter * const
painters[] = {
        &vsx_board_painter,
        &vsx_tile_painter,
        &vsx_button_painter,
        &vsx_dialog_painter,
        &vsx_note_painter,
};

#define N_PAINTERS VSX_N_ELEMENTS(painters)

struct finger {
        /* Screen position of the finger when it was pressed */
        int start_x, start_y;
        /* The last position we received */
        int last_x, last_y;
};

struct painter_data {
        void *data;
        struct vsx_listener listener;
        struct vsx_game_painter *game_painter;
};

struct vsx_game_painter {
        struct vsx_painter_toolbox toolbox;
        bool shader_data_inited;

        struct vsx_game_state *game_state;

        bool viewport_dirty;

        struct painter_data painters[N_PAINTERS];

        struct vsx_signal redraw_needed_signal;

        struct finger fingers[2];
        /* Bitmask of pressed fingers */
        int fingers_pressed;

        bool maybe_click;
};

/* Max distance in mm above which a mouse movement is no longer
 * considered a click.
 */
#define MAX_CLICK_DISTANCE 3

static void
redraw_needed_cb(struct vsx_listener *listener,
                 void *signal_data)
{
        struct painter_data *painter_data =
                vsx_container_of(listener, struct painter_data, listener);
        struct vsx_game_painter *painter = painter_data->game_painter;

        vsx_signal_emit(&painter->redraw_needed_signal, painter);
}

static void
init_redraw_needed_listener(struct vsx_game_painter *painter,
                            struct painter_data *painter_data,
                            const struct vsx_painter *callbacks)
{
        struct vsx_signal *signal =
                callbacks->get_redraw_needed_signal_cb(painter_data->data);

        painter_data->listener.notify = redraw_needed_cb;

        vsx_signal_add(signal, &painter_data->listener);
}

static void
init_painters(struct vsx_game_painter *painter)
{
        for (unsigned i = 0; i < N_PAINTERS; i++) {
                struct painter_data *painter_data = painter->painters + i;
                const struct vsx_painter *callbacks = painters[i];

                painter_data->data = callbacks->create_cb(painter->game_state,
                                                          &painter->toolbox);

                painter_data->game_painter = painter;

                if (callbacks->get_redraw_needed_signal_cb) {
                        init_redraw_needed_listener(painter,
                                                    painter_data,
                                                    callbacks);
                }
        }
}

static bool
init_toolbox(struct vsx_game_painter *painter,
             struct vsx_asset_manager *asset_manager,
             int dpi,
             struct vsx_error **error)
{
        struct vsx_painter_toolbox *toolbox = &painter->toolbox;

        if (!vsx_shader_data_init(&toolbox->shader_data,
                                  asset_manager,
                                  error))
                return false;

        painter->shader_data_inited = true;

        toolbox->image_loader = vsx_image_loader_new(asset_manager);

        toolbox->font_library = vsx_font_library_new(asset_manager, dpi, error);

        if (toolbox->font_library == NULL)
                return false;

        return true;
}

static void
destroy_toolbox(struct vsx_game_painter *painter)
{
        struct vsx_painter_toolbox *toolbox = &painter->toolbox;

        if (toolbox->font_library)
                vsx_font_library_free(toolbox->font_library);

        if (toolbox->image_loader)
                vsx_image_loader_free(toolbox->image_loader);

        if (painter->shader_data_inited)
                vsx_shader_data_destroy(&toolbox->shader_data);
}

struct vsx_game_painter *
vsx_game_painter_new(struct vsx_game_state *game_state,
                     struct vsx_asset_manager *asset_manager,
                     int dpi,
                     struct vsx_error **error)
{
        struct vsx_game_painter *painter = vsx_calloc(sizeof *painter);

        painter->game_state = game_state;

        painter->toolbox.paint_state.width = 1;
        painter->toolbox.paint_state.height = 1;
        painter->toolbox.paint_state.dpi = dpi;
        painter->viewport_dirty = true;

        vsx_signal_init(&painter->redraw_needed_signal);

        if (!init_toolbox(painter, asset_manager, dpi, error))
                goto error;

        init_painters(painter);

        return painter;

error:
        vsx_game_painter_free(painter);
        return NULL;
}

void
vsx_game_painter_set_fb_size(struct vsx_game_painter *painter,
                             int width,
                             int height)
{
        vsx_paint_state_set_fb_size(&painter->toolbox.paint_state,
                                    width, height);
        painter->viewport_dirty = true;

        for (int i = 0; i < N_PAINTERS; i++) {
                const struct vsx_painter *callbacks = painters[i];

                if (callbacks->fb_size_changed_cb == NULL)
                        continue;

                callbacks->fb_size_changed_cb(painter->painters[i].data);
        }
}

static bool
send_input_event(struct vsx_game_painter *painter,
                 const struct vsx_input_event *event)
{
        /* Try the painters in reverse order so that the topmost
         * painter will see the event first.
         */
        for (int i = N_PAINTERS - 1; i >= 0; i--) {
                const struct vsx_painter *callbacks = painters[i];

                if (callbacks->input_event_cb == NULL)
                        continue;

                if (callbacks->input_event_cb(painter->painters[i].data,
                                              event))
                        return true;
        }

        return false;
}

static void
handle_click(struct vsx_game_painter *painter,
             int x,
             int y)
{
        struct vsx_input_event event = {
                .type = VSX_INPUT_EVENT_TYPE_CLICK,
                .click = {
                        .x = x,
                        .y = y,
                },
        };

        send_input_event(painter, &event);
}

static void
set_finger_start(struct vsx_game_painter *painter,
                 int finger_num)
{
        struct finger *finger = painter->fingers + finger_num;

        finger->start_x = finger->last_x;
        finger->start_y = finger->last_y;
}

static void
store_drag_start(struct vsx_game_painter *painter)
{
        set_finger_start(painter, 0);
        set_finger_start(painter, 1);

        struct vsx_input_event event;

        switch (painter->fingers_pressed) {
        case 1:
                event.type = VSX_INPUT_EVENT_TYPE_DRAG_START;
                event.drag.x = painter->fingers[0].last_x;
                event.drag.y = painter->fingers[0].last_y;
                break;
        case 2:
                event.type = VSX_INPUT_EVENT_TYPE_DRAG_START;
                event.drag.x = painter->fingers[1].last_x;
                event.drag.y = painter->fingers[1].last_y;
                break;
        case 3:
                event.type = VSX_INPUT_EVENT_TYPE_ZOOM_START;
                event.zoom.x0 = painter->fingers[0].last_x;
                event.zoom.y0 = painter->fingers[0].last_y;
                event.zoom.x1 = painter->fingers[1].last_x;
                event.zoom.y1 = painter->fingers[1].last_y;
                break;
        default:
                return;
        }

        send_input_event(painter, &event);
}

void
vsx_game_painter_press_finger(struct vsx_game_painter *painter,
                              int finger,
                              int x,
                              int y)
{
        if (finger < 0 || finger > 1)
                return;

        painter->maybe_click = (painter->fingers_pressed == 0 &&
                                finger == 0);

        painter->fingers[finger].last_x = x;
        painter->fingers[finger].last_y = y;
        painter->fingers_pressed |= (1 << finger);

        store_drag_start(painter);
}

void
vsx_game_painter_release_finger(struct vsx_game_painter *painter,
                                int finger)
{
        if (finger < 0 || finger > 1)
                return;

        painter->fingers_pressed &= ~(1 << finger);

        store_drag_start(painter);

        if (painter->fingers_pressed == 0 && painter->maybe_click) {
                handle_click(painter,
                             painter->fingers[0].last_x,
                             painter->fingers[0].last_y);
        }

        painter->maybe_click = false;
}

static void
handle_drag(struct vsx_game_painter *painter,
            const struct finger *finger)
{
        struct vsx_input_event event = {
                .type = VSX_INPUT_EVENT_TYPE_DRAG,
                .drag = {
                        .x = finger->last_x,
                        .y = finger->last_y,
                },
        };

        send_input_event(painter, &event);
}

static void
handle_zoom(struct vsx_game_painter *painter)
{
        struct vsx_input_event event = {
                .type = VSX_INPUT_EVENT_TYPE_ZOOM,
                .zoom = {
                        .x0 = painter->fingers[0].last_x,
                        .y0 = painter->fingers[0].last_y,
                        .x1 = painter->fingers[1].last_x,
                        .y1 = painter->fingers[1].last_y,
                },
        };

        send_input_event(painter, &event);
}

void
vsx_game_painter_move_finger(struct vsx_game_painter *painter,
                             int finger,
                             int x,
                             int y)
{
        if (finger < 0 || finger > 1)
                return;

        painter->fingers[finger].last_x = x;
        painter->fingers[finger].last_y = y;

        if (painter->maybe_click && finger == 0) {
                int dx_pixels = (painter->fingers[0].last_x -
                                 painter->fingers[0].start_x);
                int dy_pixels = (painter->fingers[0].last_y -
                                 painter->fingers[0].start_y);
                float dpi = painter->toolbox.paint_state.dpi;
                float dx_mm = dx_pixels * 25.4f / dpi;
                float dy_mm = dy_pixels * 25.4f / dpi;

                if (dx_mm * dx_mm + dy_mm * dy_mm >=
                    MAX_CLICK_DISTANCE * MAX_CLICK_DISTANCE)
                        painter->maybe_click = false;
        }

        switch (painter->fingers_pressed) {
        case 1:
                handle_drag(painter, painter->fingers + 0);
                break;
        case 2:
                handle_drag(painter, painter->fingers + 1);
                break;
        case 3:
                handle_zoom(painter);
                break;
        }
}

void
vsx_game_painter_cancel_gesture(struct vsx_game_painter *painter)
{
        painter->fingers_pressed = 0;
        painter->maybe_click = false;
}

void
vsx_game_painter_paint(struct vsx_game_painter *painter)
{
        /* Preparation */

        for (unsigned i = 0; i < N_PAINTERS; i++) {
                if (painters[i]->prepare_cb == NULL)
                        continue;

                painters[i]->prepare_cb(painter->painters[i].data);
        }

        /* Painting */

        if (painter->viewport_dirty) {
                vsx_gl.glViewport(0, 0,
                                  painter->toolbox.paint_state.width,
                                  painter->toolbox.paint_state.height);

                painter->viewport_dirty = false;
        }

        vsx_gl.glClear(GL_COLOR_BUFFER_BIT);

        for (unsigned i = 0; i < N_PAINTERS; i++) {
                if (painters[i]->paint_cb == NULL)
                        continue;

                painters[i]->paint_cb(painter->painters[i].data);
        }
}

struct vsx_signal *
vsx_game_painter_get_redraw_needed_signal(struct vsx_game_painter *painter)
{
        return &painter->redraw_needed_signal;
}

static void
free_painters(struct vsx_game_painter *painter)
{
        for (unsigned i = 0; i < N_PAINTERS; i++) {
                void *data = painter->painters[i].data;

                if (data == NULL)
                        continue;

                painters[i]->free_cb(data);
        }
}

void
vsx_game_painter_free(struct vsx_game_painter *painter)
{
        free_painters(painter);

        destroy_toolbox(painter);

        vsx_free(painter);
}
