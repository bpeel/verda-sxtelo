/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2013  Neil Roberts
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

#ifndef VSX_OBJECT_H
#define VSX_OBJECT_H

typedef struct
{
  void (* free) (void *object);
} VsxObjectClass;

typedef struct
{
  const VsxObjectClass *klass;

  unsigned int ref_count;
} VsxObject;

void
vsx_object_init (void *object,
                 const VsxObjectClass *klass);

void *
vsx_object_ref (void *object);

void
vsx_object_unref (void *object);

#endif /* VSX_OBJECT_H */
