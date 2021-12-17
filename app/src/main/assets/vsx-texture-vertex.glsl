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
 
#version 100

attribute vec2 position;
attribute vec2 tex_coord_attrib;

varying mediump vec2 tex_coord;

uniform mat2 transform_matrix;
uniform vec2 translation;

void
main()
{
        gl_Position = vec4(transform_matrix * position + translation, 0.0, 1.0);
        tex_coord = tex_coord_attrib;
}

