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
#include "vsx-gl.h"
#include "vsx-board.h"

static const struct vsx_painter * const
painters[] = {
        &vsx_board_painter,
        &vsx_tile_painter,
        &vsx_button_painter,
};

#define N_PAINTERS VSX_N_ELEMENTS(painters)

struct painter_data {
        void *data;
        struct vsx_listener listener;
        struct vsx_game_painter *game_painter;
};

struct vsx_game_painter {
        struct vsx_painter_toolbox toolbox;
        bool shader_data_inited;

        bool viewport_dirty;

        struct painter_data painters[N_PAINTERS];

        struct vsx_signal redraw_needed_signal;
};

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

                painter_data->data = callbacks->create_cb(&painter->toolbox);

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
             struct vsx_error **error)
{
        struct vsx_painter_toolbox *toolbox = &painter->toolbox;

        if (!vsx_shader_data_init(&toolbox->shader_data,
                                  asset_manager,
                                  error))
                return false;

        painter->shader_data_inited = true;

        toolbox->image_loader = vsx_image_loader_new(asset_manager);

        return true;
}

static void
destroy_toolbox(struct vsx_game_painter *painter)
{
        struct vsx_painter_toolbox *toolbox = &painter->toolbox;

        if (toolbox->image_loader)
                vsx_image_loader_free(toolbox->image_loader);

        if (painter->shader_data_inited)
                vsx_shader_data_destroy(&toolbox->shader_data);
}

struct vsx_game_painter *
vsx_game_painter_new(struct vsx_asset_manager *asset_manager,
                     struct vsx_error **error)
{
        struct vsx_game_painter *painter = vsx_calloc(sizeof *painter);

        vsx_paint_state_set_fb_size(&painter->toolbox.paint_state, 1, 1);
        painter->viewport_dirty = true;

        vsx_signal_init(&painter->redraw_needed_signal);

        if (!init_toolbox(painter, asset_manager, error))
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
}

void
vsx_game_painter_paint(struct vsx_game_painter *painter,
                       struct vsx_game_state *game_state)
{
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

                painters[i]->paint_cb(painter->painters[i].data,
                                      game_state);
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
