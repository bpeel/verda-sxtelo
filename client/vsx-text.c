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
        [VSX_TEXT_GUIDE_ADD_LETTER] =
        "During your turn, click on the tile bag to add a letter to the table.",
        [VSX_TEXT_GUIDE_ADD_LETTER_WORD] =
        "QOZE",
        [VSX_TEXT_GUIDE_SHOUT] =
        "When you see a word in the jumble of letters you can take it! You "
        "don’t have to wait for your turn.\n"
        "\n"
        "Click on the megaphone to let the other players "
        "know you found a word. If you were the first one to click, your box "
        "will turn red.",
        [VSX_TEXT_GUIDE_VALID_WORDS] =
        "The word has to be at least three letters long. You can use plurals "
        "and conjugated verbs.\n",
        [VSX_TEXT_GUIDE_VALID_WORDS_WORD] =
        "MILKSATTRAINS",
        [VSX_TEXT_GUIDE_HOW_STEAL] =
        "You can also steal a word from another player!\n"
        "\n"
        "You have to steal one whole word from another player or yourself and "
        "add at least one letter from the letters in the middle to make an "
        "anagram.\n",
        [VSX_TEXT_GUIDE_HOW_STEAL_WORD] =
        "FORG",
        [VSX_TEXT_GUIDE_END] =
        "When there are no more tiles left in the bag you can keep stealing "
        "words until everyone gives up. The player with the most words wins!\n"
        "\n"
        "If there is a draw, the player with the most letters wins.\n",
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
        "• Cliquez sur les autres lettres et elles sauteront au bon endroit.\n",
        [VSX_TEXT_GUIDE_EXAMPLE_STEAL_WORD] =
        "DATEDATESSTADE",
        [VSX_TEXT_GUIDE_STEAL_WORD] =
        "Le nouveau mot ne doit pas être une autre forme du premier. "
        "Vous ne pouvez pas juste le rendre pluriel ou changer la conjugaison.",
        [VSX_TEXT_GUIDE_BOXES] =
        "Anagrams est un rapide jeu d’anagrammes entre ami·es.\n"
        "\n"
        "Tous les joueurs ont un rectangle sur l’écran. Quand votre rectangle "
        "devient vert, c’est à vous de jouer.",
        [VSX_TEXT_GUIDE_ADD_LETTER] =
        "Pendant votre tour, cliquez sur le sac à lettres pour ajouter une "
        "lettre à la table.",
        [VSX_TEXT_GUIDE_ADD_LETTER_WORD] =
        "QOZE",
        [VSX_TEXT_GUIDE_SHOUT] =
        "Quand vous voyez un mot dans le tas de lettres, vous pouvez le "
        "prendre ! Nul besoin d’attendre son tour.\n"
        "\n"
        "Cliquez sur le haut-parleur pour informer les autres "
        "joueurs que vous avez trouvé un mot. Se vous êtes le premier à "
        "cliquer, votre rectangle deviendra rouge.",
        [VSX_TEXT_GUIDE_VALID_WORDS] =
        "Le mot doit avoir au moins trois lettres. Vous pouvez utiliser "
        "des pluriels et des verbes conjugués.",
        [VSX_TEXT_GUIDE_VALID_WORDS_WORD] =
        "LAITFUSCHIENS",
        [VSX_TEXT_GUIDE_HOW_STEAL] =
        "Vous pouvez également voler un mot d’un autre joueur !\n"
        "\n"
        "Il faut prendre tout un mot d’un autre joueur ou de vous même et "
        "ajouter au moins une lettre des lettres au milieu de la table à "
        "fin de faire une anagramme\n",
        [VSX_TEXT_GUIDE_HOW_STEAL_WORD] =
        "CARN",
        [VSX_TEXT_GUIDE_END] =
        "Quand il n’y a plus de lettres dans le sac vous pouvez continuer à "
        "voler des mots jusqu’à ce que tout le monde abandonne. "
        "Le joueur avec le plus de mots remporte la partie !\n"
        "\n"
        "En cas d’égalité, le joueur avec le plus de lettres gagne.\n",
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
        "Ĉiu ludanto havas rektangulon sur la ekrano. Kiam via rektangulo "
        "verdiĝas estas via vico.",
        [VSX_TEXT_GUIDE_ADD_LETTER] =
        "Dum via vico, alklaku la literosakon por aldoni literon al la tablo.",
        [VSX_TEXT_GUIDE_ADD_LETTER_WORD] =
        "ĈOZE",
        [VSX_TEXT_GUIDE_SHOUT] =
        "Kiam vi vidas vorton en la amaso de literoj, vi povas preni ĝin! "
        "Ne necesas atendi sian vicon.\n"
        "\n"
        "Alklaku la laŭtparolilon por sciigi la aliajn ludantojn "
        "ke vi trovis vorton. Se vi estas la unua kiu klakis, via rektangulo "
        "ruĝiĝos.",
        [VSX_TEXT_GUIDE_VALID_WORDS] =
        "La vorto devas longi almenaŭ tri literojn. Ĝi povas esti nuda radiko "
        "aŭ havi finaĵojn.",
        [VSX_TEXT_GUIDE_VALID_WORDS_WORD] =
        "LAKTŜIAMALOJN",
        [VSX_TEXT_GUIDE_HOW_STEAL] =
        "Vi ankaŭ povus ŝteli vorton de alia ludanto!\n"
        "\n"
        "Necesas preni tutan vorton de alia ludanto aŭ de vi mem kaj aldoni "
        "almenaŭ unu literon el la mezo por fari anagramon.\n",
        [VSX_TEXT_GUIDE_HOW_STEAL_WORD] =
        "PERT",
        [VSX_TEXT_GUIDE_END] =
        "Kiam ne plu estas literoj en la sakoj vi povas daŭre ŝteli vortojn "
        "ĝis ĉiu rezignas. La ludanto kiu havas la plej multajn vortojn "
        "venkas!\n"
        "\n"
        "Okaze de egaleco, la ludanto kiu havas la plej multajn literojn "
        "venkas.\n",
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
