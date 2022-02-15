/*
 * Copyright (C) 2021, 2022  Neil Roberts
 */

#version 100

attribute vec2 position;

uniform mat2 transform_matrix;
uniform vec2 translation;

void
main()
{
        gl_Position = vec4(transform_matrix * position + translation, 0.0, 1.0);
}

