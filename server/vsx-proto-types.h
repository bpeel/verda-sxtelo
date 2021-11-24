/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2015, 2020, 2021  Neil Roberts
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

/* This header is included multiple times with different definitions
 * of the VSX_PROTO_TYPE macro. The macro will be called like this:
 *
 * VSX_PROTO_TYPE(enum_name - the name of the type in vsx_proto_type
 *               type_name - the name of the C type
 *               ap_type_name - the name of a type that can be used to
 *                              retrieve the value with va_arg)
 */

#define VSX_PROTO_TYPE_SIMPLE(enum_name, type_name)      \
        VSX_PROTO_TYPE(enum_name, type_name, type_name)

VSX_PROTO_TYPE(VSX_PROTO_TYPE_UINT8, guint8, unsigned int)
VSX_PROTO_TYPE(VSX_PROTO_TYPE_UINT16, guint16, unsigned int)
VSX_PROTO_TYPE_SIMPLE(VSX_PROTO_TYPE_UINT32, guint32)
VSX_PROTO_TYPE_SIMPLE(VSX_PROTO_TYPE_UINT64, guint64)
VSX_PROTO_TYPE(VSX_PROTO_TYPE_INT16, gint16, int)

#undef VSX_PROTO_TYPE_SIMPLE
