/*
 * Gemelo - A server for chatting with strangers in a foreign language
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

#ifndef __GML_BUFFER_USAGE_H__
#define __GML_BUFFER_USAGE_H__

#include <glib.h>

G_BEGIN_DECLS

typedef enum
{
  GML_BUFFER_USAGE_COPY,
  GML_BUFFER_USAGE_TAKE
} GmlBufferUsage;

G_END_DECLS

#endif /* __GML_BUFFER_USAGE_H__ */
