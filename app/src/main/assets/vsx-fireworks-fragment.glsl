/*
 * Copyright (C) 2022  Neil Roberts
 */

#version 100

precision mediump float;

uniform sampler2D tex;
varying vec4 color;

void
main()
{
        gl_FragColor = texture2D(tex, gl_PointCoord) * color;
}
