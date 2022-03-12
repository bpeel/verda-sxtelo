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

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include "vsx-util.h"
#include "vsx-buffer.h"
#include "vsx-guide.h"

typedef bool
(* vsx_instance_state_check_property_func)(const struct vsx_instance_state *s);

typedef void
(* vsx_instance_state_save_property_func)(const struct vsx_instance_state *s,
                                          struct vsx_buffer *buf);

typedef void
(* vsx_instance_state_load_property_func)(struct vsx_instance_state *s,
                                          const char *value,
                                          size_t value_length);

struct vsx_instance_state_property {
        const char *name;
        vsx_instance_state_check_property_func check;
        vsx_instance_state_save_property_func save;
        vsx_instance_state_load_property_func load;
};

static bool
load_id(const char *value,
        size_t value_length,
        uint64_t *id_out)
{
        if (value_length <= 0 || value_length > 16)
                return false;

        uint64_t id = 0;

        for (unsigned i = 0; i < value_length; i++) {
                uint8_t digit;

                if (*value >= 'a' && *value <= 'f')
                        digit = *value - 'a' + 10;
                else if (*value >= '0' && *value <= '9')
                        digit = *value - '0';
                else
                        return false;

                id = (id << 4) | digit;

                value++;
        }

        *id_out = id;

        return true;
}

static bool
check_person_id_cb(const struct vsx_instance_state *state)
{
        return state->id_type == VSX_INSTANCE_STATE_ID_TYPE_PERSON;
}

static void
save_id_cb(const struct vsx_instance_state *state,
                  struct vsx_buffer *buf)
{
        vsx_buffer_append_printf(buf, "%016" PRIx64, state->id);
}

static void
load_person_id_cb(struct vsx_instance_state *state,
                  const char *value,
                  size_t value_length)
{
        switch (state->id_type) {
        case VSX_INSTANCE_STATE_ID_TYPE_NONE:
        case VSX_INSTANCE_STATE_ID_TYPE_CONVERSATION:
                if (load_id(value,
                            value_length,
                            &state->id))
                        state->id_type = VSX_INSTANCE_STATE_ID_TYPE_PERSON;
                break;

        case VSX_INSTANCE_STATE_ID_TYPE_PERSON:
                break;
        }
}

static bool
check_conversation_id_cb(const struct vsx_instance_state *state)
{
        return state->id_type == VSX_INSTANCE_STATE_ID_TYPE_CONVERSATION;
}

static void
load_conversation_id_cb(struct vsx_instance_state *state,
                        const char *value,
                        size_t value_length)
{
        switch (state->id_type) {
        case VSX_INSTANCE_STATE_ID_TYPE_NONE:
                if (load_id(value,
                            value_length,
                            &state->id)) {
                        state->id_type =
                                VSX_INSTANCE_STATE_ID_TYPE_CONVERSATION;
                }
                break;

        case VSX_INSTANCE_STATE_ID_TYPE_CONVERSATION:
        case VSX_INSTANCE_STATE_ID_TYPE_PERSON:
                break;
        }
}

static void
save_dialog_cb(const struct vsx_instance_state *state,
                       struct vsx_buffer *buf)
{
        vsx_buffer_append_string(buf, vsx_dialog_to_name(state->dialog));
}

static void
load_dialog_cb(struct vsx_instance_state *state,
                       const char *value,
                       size_t value_length)
{
        state->dialog = vsx_dialog_from_name(value, value_length);
}

static bool
check_page_cb(const struct vsx_instance_state *state)
{
        return state->dialog == VSX_DIALOG_GUIDE;
}

static void
save_page_cb(const struct vsx_instance_state *state,
             struct vsx_buffer *buf)
{
        vsx_buffer_append_printf(buf, "%i", state->page);
}

static void
load_page_cb(struct vsx_instance_state *state,
             const char *value,
             size_t value_length)
{
        if (value_length <= 0 || value_length > 3)
                return;

        int page = 0;

        for (unsigned i = 0; i < value_length; i++) {
                if (*value >= '0' && *value <= '9')
                        page = page * 10 + *value - '0';
                else
                        return;

                value++;
        }

        if (page >= VSX_GUIDE_N_PAGES)
                return;

        state->page = page;
}

static const struct vsx_instance_state_property
properties[] = {
        {
                .name = "person_id",
                .check = check_person_id_cb,
                .save = save_id_cb,
                .load = load_person_id_cb,
        },
        {
                .name = "conversation_id",
                .check = check_conversation_id_cb,
                .save = save_id_cb,
                .load = load_conversation_id_cb,
        },
        {
                .name = "dialog",
                .save = save_dialog_cb,
                .load = load_dialog_cb,
        },
        {
                .name = "page",
                .check = check_page_cb,
                .save = save_page_cb,
                .load = load_page_cb,
        },
};

void
vsx_instance_state_init(struct vsx_instance_state *state)
{
        state->id_type = VSX_INSTANCE_STATE_ID_TYPE_NONE;
        state->id = 0;
        state->dialog = VSX_DIALOG_NONE;
        state->page = 0;
}

char *
vsx_instance_state_save(const struct vsx_instance_state *state)
{
        struct vsx_buffer buf = VSX_BUFFER_STATIC_INIT;

        for (int i = 0; i < VSX_N_ELEMENTS(properties); i++) {
                const struct vsx_instance_state_property *prop = properties + i;

                if (prop->check && !prop->check(state))
                        continue;

                if (buf.length > 0)
                        vsx_buffer_append_c(&buf, ',');

                vsx_buffer_append_string(&buf, prop->name);
                vsx_buffer_append_c(&buf, '=');

                properties[i].save(state, &buf);
        }

        vsx_buffer_append_c(&buf, '\0');

        return (char *) buf.data;
}

static const struct vsx_instance_state_property *
find_property(const char *name,
              size_t name_len)
{
        for (unsigned i = 0; i < VSX_N_ELEMENTS(properties); i++) {
                if (name_len == strlen(properties[i].name) &&
                    memcmp(properties[i].name, name, name_len) == 0)
                        return properties + i;
        }

        return NULL;
}

void
vsx_instance_state_load(struct vsx_instance_state *state,
                        const char *save_data)
{
        vsx_instance_state_init(state);

        while (true) {
                const char *end = strchr(save_data, ',');

                if (end == NULL)
                        end = save_data + strlen(save_data);

                const char *equals = memchr(save_data, '=', end - save_data);

                if (equals) {
                        const struct vsx_instance_state_property *prop =
                                find_property(save_data, equals - save_data);

                        if (prop) {
                                prop->load(state,
                                           equals + 1,
                                           end - equals - 1);
                        }
                }

                if (*end == '\0')
                        break;

                save_data = end + 1;
        }
}
