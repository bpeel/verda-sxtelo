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

#ifndef VSX_TEXT_H
#define VSX_TEXT_H

enum vsx_text_language {
        VSX_TEXT_LANGUAGE_ENGLISH,
        VSX_TEXT_LANGUAGE_FRENCH,
        VSX_TEXT_LANGUAGE_ESPERANTO,
};

enum vsx_text {
        VSX_TEXT_LANGUAGE_CODE,
        VSX_TEXT_LANGUAGE_BUTTON,
        VSX_TEXT_SHARE_BUTTON,
        VSX_TEXT_SHORT_GAME,
        VSX_TEXT_LONG_GAME,
        VSX_TEXT_LEAVE_BUTTON,
        VSX_TEXT_CANT_CHANGE_LANGUAGE_STARTED,
        VSX_TEXT_CANT_CHANGE_LENGTH_STARTED,
        VSX_TEXT_INVITE_EXPLANATION,
        VSX_TEXT_LINK_COPIED,
        VSX_TEXT_ENTER_NAME_NEW_GAME,
        VSX_TEXT_ENTER_NAME_JOIN_GAME,
        VSX_TEXT_BAD_GAME,
        VSX_TEXT_GAME_FULL,
        VSX_TEXT_PLAYER_JOINED,
        VSX_TEXT_PLAYER_LEFT,
};

#define VSX_TEXT_N_LANGUAGES 3

const char *
vsx_text_get(enum vsx_text_language language,
             enum vsx_text text);

#endif /* VSX_TEXT_H */
