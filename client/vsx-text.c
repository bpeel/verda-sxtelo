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
        [VSX_TEXT_HELP_BUTTON] = "Help",
        [VSX_TEXT_LEAVE_BUTTON] = "Leave game",
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
        [VSX_TEXT_NAME_BUTTON_NEW_GAME] =
        "Start game",
        [VSX_TEXT_NAME_BUTTON_JOIN_GAME] =
        "Join game",
        [VSX_TEXT_BAD_GAME] =
        "This game is no longer available. Please start a new one instead.",
        [VSX_TEXT_GAME_FULL] =
        "This game is full. Please start a new one instead.",
        [VSX_TEXT_PLAYER_JOINED] =
        "%s joined the game",
        [VSX_TEXT_PLAYER_LEFT] =
        "%s left the game",
        [VSX_TEXT_GUIDE_EXAMPLE_WORD] =
        "HELLO",
        [VSX_TEXT_GUIDE_MOVE_WORD] =
        "To move a word:\n"
        "\n"
        "• Drag the first letter where you want.\n"
        "• Click on the other letters and they will jump into place.\n",
        [VSX_TEXT_GUIDE_EXAMPLE_STEAL_WORD] =
        "TEARTEARSRATES",
        [VSX_TEXT_GUIDE_STEAL_WORD] =
        "The new word has to be a different root and can’t just be a different "
        "form of the original, like making it plural.",
        [VSX_TEXT_GUIDE_BOXES] =
        "Anagrams is a fast-paced word game you can play with your friends.\n"
        "\n"
        "Every player has a box on the screen. When your box is green, "
        "it is your turn.",
};

static const char *const
french[] = {
        [VSX_TEXT_LANGUAGE_CODE] = "fr",
        [VSX_TEXT_LANGUAGE_BUTTON] = "Jeu en français",
        [VSX_TEXT_SHARE_BUTTON] = "Inviter des ami·es",
        [VSX_TEXT_LONG_GAME] = "Jeu long",
        [VSX_TEXT_HELP_BUTTON] = "Aide",
        [VSX_TEXT_LEAVE_BUTTON] = "Quitter le jeu",
        [VSX_TEXT_SHORT_GAME] = "Jeu court",
        [VSX_TEXT_CANT_CHANGE_LANGUAGE_STARTED] =
        "La langue ne peut pas être changée après que le jeu a commencé",
        [VSX_TEXT_CANT_CHANGE_LENGTH_STARTED] =
        "La durée de jeu ne peut pas être changée après que le jeu a "
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
        [VSX_TEXT_NAME_BUTTON_NEW_GAME] =
        "Nouvelle partie",
        [VSX_TEXT_NAME_BUTTON_JOIN_GAME] =
        "Rejoindre",
        [VSX_TEXT_BAD_GAME] =
        "Cette partie n’est plus disponible. Veuillez en commencez une "
        "de nouveau à la place.",
        [VSX_TEXT_GAME_FULL] =
        "Cette partie est complète. Veuillez en commencez une "
        "de nouveau à la place.",
        [VSX_TEXT_PLAYER_JOINED] =
        "%s a rejoint la partie",
        [VSX_TEXT_PLAYER_LEFT] =
        "%s est parti·e",
        [VSX_TEXT_GUIDE_EXAMPLE_WORD] =
        "SALUT",
        [VSX_TEXT_GUIDE_MOVE_WORD] =
        "Pour déplacer un mot :\n"
        "\n"
        "• Faites glisser la première lettre où vous voulez.\n"
        "• Cliquez sur les autres lettres et ils sauteront au bon endroit.\n",
        [VSX_TEXT_GUIDE_EXAMPLE_STEAL_WORD] =
        "DATEDATESSTADE",
        [VSX_TEXT_GUIDE_STEAL_WORD] =
        "Le nouveau mot ne doit pas être une autre forme du premier. "
        "Vous ne pouvez pas juste le rendre pluriel ou changer la conjugaison.",
        [VSX_TEXT_GUIDE_BOXES] =
        "Anagrams est un rapide jeu d’anagrammes entre amis.\n"
        "\n"
        "Tous les joueurs ont une boîte sur l’écran. Quand votre box devient "
        "verte, c’est à vous de jouer.",
};

static const char *const
esperanto[] = {
        [VSX_TEXT_LANGUAGE_CODE] = "eo",
        [VSX_TEXT_LANGUAGE_BUTTON] = "Ludo en Esperanto",
        [VSX_TEXT_SHARE_BUTTON] = "Inviti amikojn",
        [VSX_TEXT_LONG_GAME] = "Longa ludo",
        [VSX_TEXT_SHORT_GAME] = "Mallonga ludo",
        [VSX_TEXT_HELP_BUTTON] = "Helpo",
        [VSX_TEXT_LEAVE_BUTTON] = "Forlasi la ludon",
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
        [VSX_TEXT_NAME_BUTTON_NEW_GAME] =
        "Nova ludo",
        [VSX_TEXT_NAME_BUTTON_JOIN_GAME] =
        "Aliĝi",
        [VSX_TEXT_BAD_GAME] =
        "Ĉi tiu ludo ne plu disponeblas. Bonvolu komenci novan anstataŭe.",
        [VSX_TEXT_GAME_FULL] =
        "Ĉi tiu ludo estas plena. Bonvolu komenci novan anstataŭe.",
        [VSX_TEXT_PLAYER_JOINED] =
        "%s aliĝis al la ludo",
        [VSX_TEXT_PLAYER_LEFT] =
        "%s foriris",
        [VSX_TEXT_GUIDE_EXAMPLE_WORD] =
        "STELO",
        [VSX_TEXT_GUIDE_MOVE_WORD] =
        "Por movi vorton:\n"
        "\n"
        "• Trenu la unuan literon kien vi volas.\n"
        "• Alklaku la aliajn literojn kaj ili saltos al la ĝusta loko.\n",
        [VSX_TEXT_GUIDE_EXAMPLE_STEAL_WORD] =
        "BANKBANKOKNABO",
        [VSX_TEXT_GUIDE_STEAL_WORD] =
        "La nova vorto devas esti nova radiko. Oni ne rajtas simple aldoni "
        "finaĵon al la antaŭa vorto.",
        [VSX_TEXT_GUIDE_BOXES] =
        "Verda Ŝtelo estas rapida vorta ludo kiun vi povas ludi kun viaj "
        "amikoj.\n"
        "\n"
        "Ĉiu ludanto havas skatolon sur la ekrano. Kiam via skatolo verdiĝas "
        "estas via vico.",
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
