/*
 * Gemelo - A person for chatting with strangers in a foreign language
 * Copyright (C) 2011  Neil Roberts
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

#ifndef __GML_PARSE_CONTENT_TYPE_H__
#define __GML_PARSE_CONTENT_TYPE_H__

#include <glib.h>

G_BEGIN_DECLS

typedef gboolean
(* GmlParseContentTypeGotTypeFunc) (const char *type,
                                    gpointer user_data);

typedef gboolean
(* GmlParseContentTypeGotAttributeFunc) (const char *attribute,
                                         const char *value,
                                         gpointer user_data);

gboolean
gml_parse_content_type (const char *header_value,
                        GmlParseContentTypeGotTypeFunc got_type_func,
                        GmlParseContentTypeGotAttributeFunc got_attribute_func,
                        gpointer user_data);

G_END_DECLS

#endif /* __GML_PARSE_CONTENT_TYPE_H__ */
