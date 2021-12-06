/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2011, 2013  Neil Roberts
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

#ifndef __VSX_PERSON_SET_H__
#define __VSX_PERSON_SET_H__

#include "vsx-person.h"
#include "vsx-object.h"
#include "vsx-main-context.h"
#include "vsx-netaddress.h"

typedef struct _VsxPersonSet VsxPersonSet;

VsxPersonSet *
vsx_person_set_new (void);

VsxPerson *
vsx_person_set_activate_person (VsxPersonSet *set,
                                VsxPersonId id);

VsxPerson *
vsx_person_set_get_person (VsxPersonSet *set,
                           VsxPersonId id);

VsxPerson *
vsx_person_set_generate_person (VsxPersonSet *set,
                                const char *player_name,
                                const struct vsx_netaddress *address,
                                VsxConversation *conversation);

#endif /* __VSX_PERSON_SET_H__ */
