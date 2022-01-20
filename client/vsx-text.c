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

#include "vsx-text.h"

#include <assert.h>

#include "vsx-util.h"

static const char *const
english[] = {
        [VSX_TEXT_LANGUAGE_CODE] = "en",
        [VSX_TEXT_LANGUAGE_BUTTON] = "Game in English",
        [VSX_TEXT_SHARE_BUTTON] = "Invite friends",
        [VSX_TEXT_LONG_GAME] = "Long game",
        [VSX_TEXT_SHORT_GAME] = "Short game",
        [VSX_TEXT_CANT_CHANGE_LANGUAGE_STARTED] =
        "The language can’t be changed after the game has started",
};

static const char *const
french[] = {
        [VSX_TEXT_LANGUAGE_CODE] = "fr",
        [VSX_TEXT_LANGUAGE_BUTTON] = "Jeu en français",
        [VSX_TEXT_SHARE_BUTTON] = "Inviter des ami·es",
        [VSX_TEXT_LONG_GAME] = "Jeu long",
        [VSX_TEXT_SHORT_GAME] = "Jeu court",
        [VSX_TEXT_CANT_CHANGE_LANGUAGE_STARTED] =
        "La langue ne peut pas être changée après que le jeu ait commencé",
};

static const char *const
esperanto[] = {
        [VSX_TEXT_LANGUAGE_CODE] = "eo",
        [VSX_TEXT_LANGUAGE_BUTTON] = "Ludo en Esperanto",
        [VSX_TEXT_SHARE_BUTTON] = "Inviti amikojn",
        [VSX_TEXT_LONG_GAME] = "Longa ludo",
        [VSX_TEXT_SHORT_GAME] = "Mallonga ludo",
        [VSX_TEXT_CANT_CHANGE_LANGUAGE_STARTED] =
        "Ne eblas ŝanĝi la lingvon post kiam la ludo komenciĝis",
};

static const char *const * const
languages[] = {
        [VSX_TEXT_LANGUAGE_ENGLISH] = english,
        [VSX_TEXT_LANGUAGE_FRENCH] = french,
        [VSX_TEXT_LANGUAGE_ESPERANTO] = esperanto,
};

_Static_assert(VSX_N_ELEMENTS(languages) == VSX_TEXT_N_LANGUAGES,
               "The n_languages enum needs to match the number of languagues "
               "in the array");
_Static_assert(VSX_N_ELEMENTS(english) == VSX_N_ELEMENTS(french),
               "Every string needs to be defined for every language");
_Static_assert(VSX_N_ELEMENTS(english) == VSX_N_ELEMENTS(esperanto),
               "Every string needs to be defined for every language");

const char *
vsx_text_get(enum vsx_text_language language,
             enum vsx_text text)
{
        assert(language < VSX_TEXT_N_LANGUAGES);
        assert(text < VSX_N_ELEMENTS(english));

        return languages[language][text];
}
