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

#include "vsx-instance-state.h"

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include "vsx-util.h"
#include "vsx-guide.h"

static bool
check_is_id(const struct vsx_instance_state *state,
            uint64_t test_id)
{
        if (state->id_type != VSX_INSTANCE_STATE_ID_TYPE_PERSON) {
                fprintf(stderr, "Loaded state doesn’t have a person ID\n");
                return false;
        }

        if (state->id != test_id) {
                fprintf(stderr,
                        "Person ID in loaded state does not match.\n"
                        " Expected: 0x%" PRIx64 "\n"
                        " Received: 0x%" PRIx64 "\n",
                        test_id,
                        state->id);
                return false;
        }

        return true;
}

static bool
test_person_id(uint64_t test_id)
{
        struct vsx_instance_state state;

        vsx_instance_state_init(&state);

        state.id_type = VSX_INSTANCE_STATE_ID_TYPE_PERSON;
        state.id = test_id;

        char *str = vsx_instance_state_save(&state);

        struct vsx_instance_state loaded_state;

        memset(&loaded_state, 0, sizeof loaded_state);

        vsx_instance_state_load(&loaded_state, str);

        vsx_free(str);

        return check_is_id(&loaded_state, test_id);
}

static bool
test_long_person_id(void)
{
        struct vsx_instance_state state;

        vsx_instance_state_init(&state);

        vsx_instance_state_load(&state, "person_id=0123456789abcdeff");

        if (state.id_type != VSX_INSTANCE_STATE_ID_TYPE_NONE) {
                fprintf(stderr,
                        "State has a person ID set from a state that is too "
                        "long\n");
                return false;
        }

        return true;
}

static bool
test_short_person_id(void)
{
        struct vsx_instance_state state;

        vsx_instance_state_init(&state);

        vsx_instance_state_load(&state, "person_id=5");

        if (state.id_type != VSX_INSTANCE_STATE_ID_TYPE_PERSON) {
                fprintf(stderr,
                        "State doesn’t have a person ID after load\n");
                return false;
        }

        if (state.id != 5) {
                fprintf(stderr,
                        "Person ID does not match.\n"
                        " Expected: 0x%" PRIx64 "\n"
                        " Received: 0x%" PRIx64 "\n",
                        UINT64_C(5),
                        state.id);
                return false;
        }

        return true;
}

static bool
test_invalid_char_in_person_id(void)
{
        bool ret = true;

        static char invalid_chars[] = "`g/:";

        for (const char *p = invalid_chars; *p; p++) {
                struct vsx_instance_state state;

                vsx_instance_state_init(&state);

                char str[] = "person_id=0?";

                str[(sizeof str) - 2] = *p;

                vsx_instance_state_load(&state, str);

                if (state.id_type != VSX_INSTANCE_STATE_ID_TYPE_NONE) {
                        fprintf(stderr,
                                "State has a person ID after invalid "
                                "load of data “%s”\n",
                                str);
                        ret = false;
                }
        }

        return ret;
}

static bool
test_person_id_first_prop(void)
{
        struct vsx_instance_state loaded_state;

        memset(&loaded_state, 0, sizeof loaded_state);

        vsx_instance_state_load(&loaded_state, "person_id=5,wibble=7");

        return check_is_id(&loaded_state, 5);
}

static bool
test_person_id_second_prop(void)
{
        struct vsx_instance_state loaded_state;

        memset(&loaded_state, 0, sizeof loaded_state);

        vsx_instance_state_load(&loaded_state, "customers=7,person_id=5");

        return check_is_id(&loaded_state, 5);
}

static bool
test_load_two_person_ids(void)
{
        struct vsx_instance_state loaded_state;

        memset(&loaded_state, 0, sizeof loaded_state);

        vsx_instance_state_load(&loaded_state, "person_id=5,person_id=6");

        return check_is_id(&loaded_state, 5);
}

static bool
test_no_equals(void)
{
        struct vsx_instance_state loaded_state;

        memset(&loaded_state, 0, sizeof loaded_state);

        vsx_instance_state_load(&loaded_state, "what_is_this,person_id=5");

        return check_is_id(&loaded_state, 5);
}

static bool
test_empty_person_id(void)
{
        struct vsx_instance_state loaded_state;

        memset(&loaded_state, 0, sizeof loaded_state);

        vsx_instance_state_load(&loaded_state, "person_id=");

        if (loaded_state.id_type != VSX_INSTANCE_STATE_ID_TYPE_NONE) {
                fprintf(stderr,
                        "State has a person ID after an empty value was set\n");
                return false;
        }

        return true;
}

static bool
test_conversation_id(uint64_t test_id)
{
        struct vsx_instance_state state;

        vsx_instance_state_init(&state);

        state.id_type = VSX_INSTANCE_STATE_ID_TYPE_CONVERSATION;
        state.id = test_id;

        char *str = vsx_instance_state_save(&state);

        struct vsx_instance_state loaded_state;

        memset(&loaded_state, 0, sizeof loaded_state);

        vsx_instance_state_load(&loaded_state, str);

        vsx_free(str);

        if (loaded_state.id_type != VSX_INSTANCE_STATE_ID_TYPE_CONVERSATION) {
                fprintf(stderr,
                        "Loaded state doesn’t have a "
                        "conversation ID\n");
                return false;
        }

        if (loaded_state.id != test_id) {
                fprintf(stderr,
                        "Conversation ID in loaded state does not match.\n"
                        " Expected: 0x%" PRIx64 "\n"
                        " Received: 0x%" PRIx64 "\n",
                        test_id,
                        loaded_state.id);
                return false;
        }

        return true;
}

static bool
test_invalid_char_in_conversation_id(void)
{
        struct vsx_instance_state state;

        vsx_instance_state_init(&state);

        vsx_instance_state_load(&state, "conversation_id=0?");

        if (state.id_type != VSX_INSTANCE_STATE_ID_TYPE_NONE) {
                fprintf(stderr,
                        "State has a conversation ID after invalid "
                        "load of data");
                return false;
        }

        return true;
}

static bool
test_conversation_id_doesnt_override_person(void)
{
        struct vsx_instance_state state;

        vsx_instance_state_init(&state);

        vsx_instance_state_load(&state, "person_id=5,conversation_id=6");

        return check_is_id(&state, 5);
}

static bool
test_empty_string(void)
{
        struct vsx_instance_state loaded_state;

        memset(&loaded_state, 0, sizeof loaded_state);

        vsx_instance_state_load(&loaded_state, "");

        if (loaded_state.id_type != VSX_INSTANCE_STATE_ID_TYPE_NONE) {
                fprintf(stderr,
                        "State has a person ID after an empty string was "
                        "loaded\n");
                return false;
        }

        return true;
}

static bool
test_save_empty(void)
{
        struct vsx_instance_state state;

        vsx_instance_state_init(&state);
        char *str = vsx_instance_state_save(&state);

        struct vsx_instance_state loaded_state;

        memset(&loaded_state, 0, sizeof loaded_state);

        vsx_instance_state_load(&loaded_state, "");

        vsx_free(str);

        if (loaded_state.id != VSX_INSTANCE_STATE_ID_TYPE_NONE) {
                fprintf(stderr,
                        "State has an ID after loading init state\n");
                return false;
        }

        return true;
}

static bool
test_dialog(enum vsx_dialog value_to_set)
{
        struct vsx_instance_state state;

        vsx_instance_state_init(&state);

        bool ret = true;

        if (state.dialog != VSX_DIALOG_NONE) {
                fprintf(stderr,
                        "dialog did not start off as NONE\n");
                ret = false;
        }

        state.dialog = value_to_set;

        char *str = vsx_instance_state_save(&state);

        struct vsx_instance_state loaded_state;

        memset(&loaded_state, 0, sizeof loaded_state);

        vsx_instance_state_load(&loaded_state, str);

        vsx_free(str);

        if (loaded_state.dialog != value_to_set) {
                fprintf(stderr,
                        "dialog has wrong value after load.\n"
                        " Expected: %i (%s)\n"
                        " Received: %i (%s)\n",
                        value_to_set,
                        vsx_dialog_to_name(value_to_set),
                        loaded_state.dialog,
                        vsx_dialog_to_name(loaded_state.dialog));
                ret = false;
        }

        return ret;
}

static bool
test_dialog_invalid_value(const char *value)
{
        struct vsx_instance_state state;

        vsx_instance_state_init(&state);

        enum vsx_dialog old_value = state.dialog;

        char *str = vsx_strconcat("dialog=", value, NULL);
        vsx_instance_state_load(&state, str);
        vsx_free(str);

        if (state.dialog != old_value) {
                fprintf(stderr,
                        "dialog changed after setting "
                        "the invalid value “%s”\n",
                        value);
                return false;
        }

        return true;
}

static bool
test_page_value(int page_value)
{
        struct vsx_instance_state state;
        bool ret = true;

        vsx_instance_state_init(&state);

        if (state.page != 0) {
                fprintf(stderr, "Initial page value not zero.\n");
                ret = false;
        }

        state.page = page_value;
        state.dialog = VSX_DIALOG_GUIDE;

        char *str = vsx_instance_state_save(&state);

        struct vsx_instance_state loaded_state;

        memset(&loaded_state, 0, sizeof loaded_state);

        vsx_instance_state_load(&loaded_state, str);

        vsx_free(str);

        if (loaded_state.page != page_value) {
                fprintf(stderr,
                        "page has wrong value after load.\n"
                        " Expected: %i\n"
                        " Received: %i\n",
                        page_value,
                        loaded_state.page);
                ret = false;
        }

        return ret;
}

static bool
test_pages(void)
{
        bool ret = true;

        for (int i = 0; i < VSX_GUIDE_N_PAGES; i++) {
                if (!test_page_value(i))
                        ret = false;
        }

        return ret;
}

static bool
test_page_invalid_value(const char *value)
{
        struct vsx_instance_state state;

        vsx_instance_state_init(&state);

        int old_value = state.page;

        char *str = vsx_strconcat("page=", value, NULL);
        vsx_instance_state_load(&state, str);
        vsx_free(str);

        if (state.page != old_value) {
                fprintf(stderr,
                        "page changed after setting "
                        "the invalid value “%s”\n",
                        value);
                return false;
        }

        return true;
}

int
main(int argc, char **argv)
{
        int ret = EXIT_SUCCESS;

        if (!test_person_id(UINT64_C(0x8182838485868788)))
                ret = EXIT_FAILURE;
        if (!test_person_id(UINT64_MAX))
                ret = EXIT_FAILURE;
        if (!test_person_id(0))
                ret = EXIT_FAILURE;
        if (!test_person_id(UINT64_C(0xfedcba9876543210)))
                ret = EXIT_FAILURE;

        if (!test_long_person_id())
                ret = EXIT_FAILURE;

        if (!test_short_person_id())
                ret = EXIT_FAILURE;

        if (!test_invalid_char_in_person_id())
                ret = EXIT_FAILURE;

        if (!test_person_id_first_prop())
                ret = EXIT_FAILURE;

        if (!test_person_id_second_prop())
                ret = EXIT_FAILURE;

        if (!test_load_two_person_ids())
                ret = EXIT_FAILURE;

        if (!test_conversation_id(UINT64_C(0x1234567890abcdef)))
                ret = EXIT_FAILURE;

        if (!test_invalid_char_in_conversation_id())
                ret = EXIT_FAILURE;

        if (!test_conversation_id_doesnt_override_person())
                ret = EXIT_FAILURE;

        if (!test_no_equals())
                ret = EXIT_FAILURE;

        if (!test_empty_person_id())
                ret = EXIT_FAILURE;

        if (!test_empty_string())
                ret = EXIT_FAILURE;

        if (!test_save_empty())
                ret = EXIT_FAILURE;

        if (!test_dialog(VSX_DIALOG_NONE))
                ret = EXIT_FAILURE;

        if (!test_dialog(VSX_DIALOG_INVITE_LINK))
                ret = EXIT_FAILURE;

        if (!test_pages())
                ret = EXIT_FAILURE;

        if (!test_dialog_invalid_value(""))
                ret = EXIT_FAILURE;

        if (!test_dialog_invalid_value("really_long_value"))
                ret = EXIT_FAILURE;

        if (!test_dialog_invalid_value("invitey"))
                ret = EXIT_FAILURE;

        if (!test_dialog_invalid_value("InVite"))
                ret = EXIT_FAILURE;

        if (!test_page_invalid_value(""))
                ret = EXIT_FAILURE;

        if (!test_page_invalid_value("1111"))
                ret = EXIT_FAILURE;

        if (!test_page_invalid_value("999"))
                ret = EXIT_FAILURE;

        if (!test_page_invalid_value("a"))
                ret = EXIT_FAILURE;

        if (!test_page_invalid_value(" "))
                ret = EXIT_FAILURE;

        return ret;
}
