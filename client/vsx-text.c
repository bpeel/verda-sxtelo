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
        [VSX_TEXT_CANT_CHANGE_LENGTH_STARTED] =
        "The game length can’t be changed after the game has started",
        [VSX_TEXT_INVITE_EXPLANATION] =
        "Send the link below or scan the code to invite friends.",
        [VSX_TEXT_LINK_COPIED] =
        "Link copied to the clipboard",
        [VSX_TEXT_ENTER_NAME_NEW_GAME] =
        "Please enter your name to start a new game.",
        [VSX_TEXT_ENTER_NAME_JOIN_GAME] =
        "Please enter your name to join the game.",
        [VSX_TEXT_BAD_GAME] =
        "This game is no longer available. Please start a new one instead.",
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
        [VSX_TEXT_CANT_CHANGE_LENGTH_STARTED] =
        "La durée de jeu ne peut pas être changée après que le jeu ait "
        "commencé",
        [VSX_TEXT_INVITE_EXPLANATION] =
        "Envoyez le lien ci-dessous ou flashez le code pour inviter "
        "des ami·es.",
        [VSX_TEXT_LINK_COPIED] =
        "Le lien a été copié",
        [VSX_TEXT_ENTER_NAME_NEW_GAME] =
        "Veuillez saisir votre nom pour commencer une nouvelle partie.",
        [VSX_TEXT_ENTER_NAME_JOIN_GAME] =
        "Veuillez saisir votre nom pour rejoindre la partie.",
        [VSX_TEXT_BAD_GAME] =
        "Cette partie n’est plus disponible. Veuillez en commencez une "
        "de nouveau à la place.",
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
        [VSX_TEXT_CANT_CHANGE_LENGTH_STARTED] =
        "Ne eblas ŝanĝi la longecon de la ludo post kiam la ludo komenciĝis",
        [VSX_TEXT_INVITE_EXPLANATION] =
        "Sendu la jenan ligilon aŭ skanu la kodon por inviti amikojn.",
        [VSX_TEXT_LINK_COPIED] =
        "La ligilo estis kopiita",
        [VSX_TEXT_ENTER_NAME_NEW_GAME] =
        "Bonvolu entajpi vian nomon por komenci novan ludon.",
        [VSX_TEXT_ENTER_NAME_JOIN_GAME] =
        "Bonvolu entajpi vian nomon por aliĝi al la ludo.",
        [VSX_TEXT_BAD_GAME] =
        "Ĉi tiu ludo ne plu disponeblas. Bonvolu komenci novan anstataŭe.",
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
