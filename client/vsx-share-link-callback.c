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

#include "vsx-share-link-callback.h"

static vsx_share_link_callback callback_func;
static void *callback_data;

void
vsx_share_link_set_callback(vsx_share_link_callback callback,
                            void *user_data)
{
        callback_func = callback;
        callback_data = user_data;
}

void
vsx_share_link(struct vsx_game_state *game_state,
               const char *link)
{
        if (callback_func == NULL)
                return;

        callback_func(link, callback_data);
}
