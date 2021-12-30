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

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>

#include "vsx-conversation-set.h"
#include "vsx-util.h"

static bool
test_get_from_empty_set(VsxConversationSet *set)
{
        VsxConversation *conversation =
                vsx_conversation_set_get_conversation(set, 42);

        if (conversation != NULL) {
                fprintf(stderr,
                        "A conversation was retrieved from an empty set.\n");
                return false;
        }

        return true;
}

static bool
using_esperanto_tiles(VsxConversation *conversation)
{
        for (int i = 0; i < VSX_TILE_DATA_N_TILES; i++) {
                if (!strcmp(conversation->tiles[i].letter, "Ĉ"))
                        return true;
        }

        fprintf(stderr,
                "The conversation doesn’t seem to be using the "
                "Esperanto tile set.\n");

        return false;
}

static bool
using_english_tiles(VsxConversation *conversation)
{
        for (int i = 0; i < VSX_TILE_DATA_N_TILES; i++) {
                if (!strcmp(conversation->tiles[i].letter, "W"))
                        return true;
        }

        fprintf(stderr,
                "The conversation doesn’t seem to be using the "
                "English tile set.\n");

        return false;
}

static bool
test_join_same_room(VsxConversationSet *set,
                    VsxConversation *conversation,
                    const struct vsx_netaddress *addr)
{
        VsxConversation *other_conv =
                vsx_conversation_set_get_pending_conversation(set,
                                                              "eo:default",
                                                              addr);

        bool ret = true;

        if (other_conv != conversation) {
                fprintf(stderr,
                        "A different conversation was received after "
                        "joining the same room.\n");
                ret = false;
        }

        vsx_object_unref(other_conv);

        return ret;
}

static bool
test_join_after_starting(VsxConversationSet *set,
                         VsxConversation *conversation,
                         const struct vsx_netaddress *addr)
{
        vsx_conversation_start(conversation);

        VsxConversation *other_conv =
                vsx_conversation_set_get_pending_conversation(set,
                                                              "eo:default",
                                                              addr);

        bool ret = true;

        if (other_conv == conversation) {
                fprintf(stderr,
                        "The same conversation was received after "
                        "joining the same room as the conversation after "
                        "it started.\n");
                ret = false;
        } else if (other_conv->hash_entry.id == conversation->hash_entry.id) {
                fprintf(stderr,
                        "Two different conversations have the same ID.\n");
                ret = false;
        }

        if (!using_esperanto_tiles(other_conv))
                ret = false;

        vsx_object_unref(other_conv);

        return ret;
}

static bool
test_get_by_id(VsxConversationSet *set,
               VsxConversation *conversation)
{
        VsxConversationId id = conversation->hash_entry.id;
        VsxConversation *other = vsx_conversation_set_get_conversation(set, id);

        if (other == NULL) {
                fprintf(stderr,
                        "The conversation set couldn’t find the conversation "
                        "by ID.\n");
                return false;
        }

        if (other != conversation) {
                fprintf(stderr,
                        "The conversation set found the wrong conversation.\n");
                return false;
        }

        return true;
}

static bool
test_generate_conversation(VsxConversationSet *set,
                           const struct vsx_netaddress *addr)
{
        VsxConversation *conversation =
                vsx_conversation_set_generate_conversation(set, "vo", addr);

        bool ret = true;

        if (!using_esperanto_tiles(conversation)) {
                ret = false;
                goto out;
        }

        if (!test_get_by_id(set, conversation)) {
                ret = false;
                goto out;
        }

out:
        vsx_object_unref(conversation);
        return ret;
}

static bool
test_generate_english_conversation(VsxConversationSet *set,
                                   const struct vsx_netaddress *addr)
{
        VsxConversation *conversation =
                vsx_conversation_set_generate_conversation(set, "en", addr);

        bool ret = true;

        if (!using_english_tiles(conversation)) {
                ret = false;
                goto out;
        }

        if (!test_get_by_id(set, conversation)) {
                ret = false;
                goto out;
        }

out:
        vsx_object_unref(conversation);
        return ret;
}

static bool
test_no_language_code(VsxConversationSet *set,
                      const struct vsx_netaddress *addr)
{
        VsxConversation *conversation =
                vsx_conversation_set_get_pending_conversation(set,
                                                              "what",
                                                              addr);

        bool ret = true;

        if (!using_esperanto_tiles(conversation)) {
                ret = false;
                goto out;
        }

out:
        vsx_object_unref(conversation);
        return ret;
}

static bool
test_abandon_game(VsxConversationSet *set,
                  const struct vsx_netaddress *addr)
{
        VsxConversation *conversation =
                vsx_conversation_set_get_pending_conversation(set,
                                                              "vo:what",
                                                              addr);

        bool ret = true;

        if (!using_esperanto_tiles(conversation)) {
                ret = false;
                goto out;
        }

        VsxPlayer *player =
                vsx_conversation_add_player(conversation, "Zamenhof");

        vsx_conversation_player_left(conversation, player->num);

        VsxConversation *other_conv =
                vsx_conversation_set_get_pending_conversation(set,
                                                              "vo:what",
                                                              addr);

        if (other_conv == conversation) {
                fprintf(stderr,
                        "Managed to join conversation after everyone left "
                        "it.\n");
                ret = false;
        }

        vsx_object_unref(other_conv);

out:
        vsx_object_unref(conversation);
        return ret;
}

static bool
test_free_game(VsxConversationSet *set,
               const struct vsx_netaddress *addr)
{
        VsxConversation *conversation =
                vsx_conversation_set_generate_conversation(set,
                                                           "en",
                                                           addr);

        bool ret = true;

        VsxPlayer *player =
                vsx_conversation_add_player(conversation, "Zamenhof");

        vsx_conversation_start(conversation);

        vsx_conversation_player_left(conversation, player->num);

        VsxConversationId id = conversation->hash_entry.id;
        VsxConversation *other_conv =
                vsx_conversation_set_get_conversation(set, id);

        if (other_conv != NULL) {
                fprintf(stderr,
                        "Managed to retrieve conversation after everyone left "
                        "it.\n");
                ret = false;
        }

        vsx_object_unref(conversation);

        return ret;
}

static bool
run_tests(VsxConversationSet *set)
{
        struct vsx_netaddress addr;

        if (!vsx_netaddress_from_string(&addr,
                                        "127.0.0.1", 1234)) {
                fprintf(stderr, "netaddress_from_string failed\n");
                return false;
        }

        if (!test_get_from_empty_set(set))
                return false;

        bool ret = true;

        VsxConversation *conversation =
                vsx_conversation_set_get_pending_conversation(set,
                                                              "eo:default",
                                                              &addr);

        if (!using_esperanto_tiles(conversation)) {
                ret = false;
                goto out;
        }

        if (!test_join_same_room(set, conversation, &addr)) {
                ret = false;
                goto out;
        }

        if (!test_get_by_id(set, conversation)) {
                ret = false;
                goto out;
        }

        if (!test_join_after_starting(set, conversation, &addr)) {
                ret = false;
                goto out;
        }

        /* Make sure we can still get the conversation by its ID even
         * after it is no longer “pending” (because it has started)
         */
        if (!test_get_by_id(set, conversation)) {
                ret = false;
                goto out;
        }

        if (!test_generate_conversation(set, &addr)) {
                ret = false;
                goto out;
        }

        if (!test_generate_english_conversation(set, &addr)) {
                ret = false;
                goto out;
        }

        if (!test_no_language_code(set, &addr)) {
                ret = false;
                goto out;
        }

        if (!test_abandon_game(set, &addr)) {
                ret = false;
                goto out;
        }

        if (!test_free_game(set, &addr)) {
                ret = false;
                goto out;
        }

out:
        vsx_object_unref(conversation);
        return ret;
}

int
main(int argc, char **argv)
{
        int ret = EXIT_SUCCESS;

        VsxConversationSet *set = vsx_conversation_set_new();

        if (!run_tests(set))
                ret = EXIT_FAILURE;

        vsx_object_unref(set);

        return ret;
}
