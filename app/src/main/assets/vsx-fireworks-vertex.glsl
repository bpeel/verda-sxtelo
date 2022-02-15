/*
 * Copyright (C) 2022  Neil Roberts
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

