/*
 * Copyright (C) 2014, 2021  Neil Roberts
 */

#version 100

precision mediump float;

varying vec2 tex_coord;

uniform sampler2D tex;

void
main()
{
        gl_FragColor = texture2D(tex, tex_coord);
}
