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

#include "vsx-dialog.h"

#include <string.h>

#include "vsx-util.h"

static const char * const
names[] = {
        [VSX_DIALOG_NONE] = "none",
        [VSX_DIALOG_MENU] = "menu",
        [VSX_DIALOG_INVITE_LINK] = "invite",
};

enum vsx_dialog
vsx_dialog_from_name(const char *name, size_t name_length)
{
        for (int i = 0; i < VSX_N_ELEMENTS(names); i++) {
                if (strlen(names[i]) == name_length &&
                    !memcmp(names[i], name, name_length))
                        return i;
        }

        return VSX_DIALOG_NONE;
}

const char *
vsx_dialog_to_name(enum vsx_dialog dialog)
{
        return names[dialog];
}
