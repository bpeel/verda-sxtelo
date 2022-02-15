/*
 * Copyright (C) 2021  Neil Roberts
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

