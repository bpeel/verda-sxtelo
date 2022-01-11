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
check_person_id_cb(const struct vsx_instance_state *state)
{
        return state->has_person_id;
}

static void
save_person_id_cb(const struct vsx_instance_state *state,
                  struct vsx_buffer *buf)
{
        vsx_buffer_append_printf(buf, "%016" PRIx64, state->person_id);
}

static void
load_person_id_cb(struct vsx_instance_state *state,
                  const char *value,
                  size_t value_length)
{
        if (value_length <= 0 || value_length > 16)
                return;

        uint64_t id = 0;

        for (unsigned i = 0; i < value_length; i++) {
                uint8_t digit;

                if (*value >= 'a' && *value <= 'f')
                        digit = *value - 'a' + 10;
                else if (*value >= '0' && *value <= '9')
                        digit = *value - '0';
                else
                        return;

                id = (id << 4) | digit;

                value++;
        }

        state->has_person_id = true;
        state->person_id = id;
}

static void
save_invite_visible_cb(const struct vsx_instance_state *state,
                       struct vsx_buffer *buf)
{
        vsx_buffer_append_c(buf, state->invite_visible ? 'y' : 'n');
}

static void
load_invite_visible_cb(struct vsx_instance_state *state,
                       const char *value,
                       size_t value_length)
{
        if (value_length != 1)
                return;

        if (*value == 'y')
                state->invite_visible = true;
        else if (*value == 'n')
                state->invite_visible = false;
}

static const struct vsx_instance_state_property
properties[] = {
        {
                .name = "person_id",
                .check = check_person_id_cb,
                .save = save_person_id_cb,
                .load = load_person_id_cb,
        },
        {
                .name = "invite_visible",
                .save = save_invite_visible_cb,
                .load = load_invite_visible_cb,
        },
};

void
vsx_instance_state_init(struct vsx_instance_state *state)
{
        state->has_person_id = false;
        state->invite_visible = true;
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
