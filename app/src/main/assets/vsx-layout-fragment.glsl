/*
 * Copyright (C) 2021, 2022  Neil Roberts
 */

#version 100

precision mediump float;

varying vec2 tex_coord;

uniform sampler2D tex;
uniform vec3 color;

void
main()
{
        gl_FragColor = vec4(color, texture2D(tex, tex_coord).a);
}
