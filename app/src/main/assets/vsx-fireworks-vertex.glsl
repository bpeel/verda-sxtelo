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
#version 100

attribute vec2 position;
attribute vec3 color_attrib;

uniform mat2 transform_matrix;
uniform vec2 translation;
uniform vec2 start_point;
uniform float point_size;
uniform float elapsed_time;

varying vec4 color;

#define PI 3.1415926538

#define GRAVITY 900.0

void
main()
{
        vec2 average_velocity =
                vec2(position.x, position.y + elapsed_time * GRAVITY / 2.0);

        vec2 board_pos = start_point + average_velocity * elapsed_time;

        gl_Position = vec4(transform_matrix * board_pos + translation,
                           0.0,
                           1.0);

        gl_PointSize = point_size;

        color = vec4(color_attrib, cos(elapsed_time * PI / 2.0));
}

