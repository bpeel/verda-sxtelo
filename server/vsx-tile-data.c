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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>

#include "vsx-tile-data.h"

/* Location of the upper-left tile */
#define X 99
#define Y 92
/* Distance between each tile */
#define W 23
#define H 23

const VsxTile
vsx_tile_data[VSX_TILE_DATA_N_TILES] =
  {
    { X+0*W,Y+0*H, "A", FALSE }, { X+1*W,Y+0*H, "A", FALSE },
    { X+2*W,Y+0*H, "A", FALSE }, { X+3*W,Y+0*H, "A", FALSE },
    { X+4*W,Y+0*H, "A", FALSE }, { X+5*W,Y+0*H, "A", FALSE },
    { X+6*W,Y+0*H, "A", FALSE }, { X+7*W,Y+0*H, "A", FALSE },
    { X+8*W,Y+0*H, "A", FALSE }, { X+9*W,Y+0*H, "B", FALSE },
    { X+10*W,Y+0*H, "B", FALSE }, { X+11*W,Y+0*H, "B", FALSE },
    { X+12*W,Y+0*H, "C", FALSE }, { X+13*W,Y+0*H, "C", FALSE },
    { X+14*W,Y+0*H, "Ĉ", FALSE }, { X+15*W,Y+0*H, "Ĉ", FALSE },
    { X+16*W,Y+0*H, "D", FALSE },

    { X+0*W,Y+1*H, "D", FALSE }, { X+1*W,Y+1*H, "D", FALSE },
    { X+2*W,Y+1*H, "E", FALSE }, { X+3*W,Y+1*H, "E", FALSE },
    { X+4*W,Y+1*H, "E", FALSE }, { X+5*W,Y+1*H, "E", FALSE },
    { X+6*W,Y+1*H, "E", FALSE }, { X+7*W,Y+1*H, "E", FALSE },
    { X+8*W,Y+1*H, "E", FALSE }, { X+9*W,Y+1*H, "E", FALSE },
    { X+10*W,Y+1*H, "F", FALSE }, { X+11*W,Y+1*H, "F", FALSE },
    { X+12*W,Y+1*H, "F", FALSE }, { X+13*W,Y+1*H, "G", FALSE },
    { X+14*W,Y+1*H, "G", FALSE }, { X+15*W,Y+1*H, "Ĝ", FALSE },
    { X+16*W,Y+1*H, "Ĝ", FALSE },

    { X+0*W,Y+2*H, "H", FALSE }, { X+1*W,Y+2*H, "H", FALSE },
    { X+2*W,Y+2*H, "Ĥ", FALSE }, { X+3*W,Y+2*H, "I", FALSE },
    { X+4*W,Y+2*H, "I", FALSE }, { X+5*W,Y+2*H, "I", FALSE },
    { X+6*W,Y+2*H, "I", FALSE }, { X+7*W,Y+2*H, "I", FALSE },
    { X+8*W,Y+2*H, "I", FALSE }, { X+9*W,Y+2*H, "I", FALSE },
    { X+10*W,Y+2*H, "I", FALSE }, { X+11*W,Y+2*H, "I", FALSE },
    { X+12*W,Y+2*H, "I", FALSE }, { X+13*W,Y+2*H, "J", FALSE },
    { X+14*W,Y+2*H, "J", FALSE }, { X+15*W,Y+2*H, "J", FALSE },
    { X+16*W,Y+2*H, "J", FALSE },

    { X+0*W,Y+3*H, "J", FALSE }, { X+1*W,Y+3*H, "Ĵ", FALSE },
    { X+2*W,Y+3*H, "K", FALSE }, { X+3*W,Y+3*H, "K", FALSE },
    { X+4*W,Y+3*H, "K", FALSE }, { X+5*W,Y+3*H, "K", FALSE },
    { X+6*W,Y+3*H, "K", FALSE }, { X+7*W,Y+3*H, "K", FALSE },
    { X+8*W,Y+3*H, "L", FALSE }, { X+9*W,Y+3*H, "L", FALSE },
    { X+10*W,Y+3*H, "L", FALSE }, { X+11*W,Y+3*H, "L", FALSE },
    { X+12*W,Y+3*H, "M", FALSE }, { X+13*W,Y+3*H, "M", FALSE },
    { X+14*W,Y+3*H, "M", FALSE }, { X+15*W,Y+3*H, "M", FALSE },
    { X+16*W,Y+3*H, "M", FALSE },

    { X+0*W,Y+4*H, "M", FALSE }, { X+1*W,Y+4*H, "N", FALSE },
    { X+2*W,Y+4*H, "N", FALSE }, { X+3*W,Y+4*H, "N", FALSE },
    { X+4*W,Y+4*H, "N", FALSE }, { X+5*W,Y+4*H, "N", FALSE },
    { X+6*W,Y+4*H, "N", FALSE }, { X+7*W,Y+4*H, "N", FALSE },
    { X+8*W,Y+4*H, "N", FALSE }, { X+9*W,Y+4*H, "O", FALSE },
    { X+10*W,Y+4*H, "O", FALSE }, { X+11*W,Y+4*H, "O", FALSE },
    { X+12*W,Y+4*H, "O", FALSE }, { X+13*W,Y+4*H, "O", FALSE },
    { X+14*W,Y+4*H, "O", FALSE }, { X+15*W,Y+4*H, "O", FALSE },
    { X+16*W,Y+4*H, "O", FALSE },

    { X+0*W,Y+5*H, "O", FALSE }, { X+1*W,Y+5*H, "O", FALSE },
    { X+2*W,Y+5*H, "O", FALSE }, { X+3*W,Y+5*H, "P", FALSE },
    { X+4*W,Y+5*H, "P", FALSE }, { X+5*W,Y+5*H, "P", FALSE },
    { X+6*W,Y+5*H, "P", FALSE }, { X+7*W,Y+5*H, "P", FALSE },
    { X+8*W,Y+5*H, "R", FALSE }, { X+9*W,Y+5*H, "R", FALSE },
    { X+10*W,Y+5*H, "R", FALSE }, { X+11*W,Y+5*H, "R", FALSE },
    { X+12*W,Y+5*H, "R", FALSE }, { X+13*W,Y+5*H, "R", FALSE },
    { X+14*W,Y+5*H, "R", FALSE }, { X+15*W,Y+5*H, "S", FALSE },
    { X+16*W,Y+5*H, "S", FALSE },

    { X+0*W,Y+6*H, "S", FALSE }, { X+1*W,Y+6*H, "S", FALSE },
    { X+2*W,Y+6*H, "S", FALSE }, { X+3*W,Y+6*H, "S", FALSE },
    { X+4*W,Y+6*H, "S", FALSE }, { X+5*W,Y+6*H, "Ŝ", FALSE },
    { X+6*W,Y+6*H, "Ŝ", FALSE }, { X+7*W,Y+6*H, "T", FALSE },
    { X+8*W,Y+6*H, "T", FALSE }, { X+9*W,Y+6*H, "T", FALSE },
    { X+10*W,Y+6*H, "T", FALSE }, { X+11*W,Y+6*H, "T", FALSE },
    { X+12*W,Y+6*H, "U", FALSE }, { X+13*W,Y+6*H, "U", FALSE },
    { X+14*W,Y+6*H, "U", FALSE }, { X+15*W,Y+6*H, "Ŭ", FALSE },
    { X+16*W,Y+6*H, "Ŭ", FALSE },

    { X+0*W,Y+7*H, "V", FALSE }, { X+1*W,Y+7*H, "V", FALSE },
    { X+2*W,Y+7*H, "Z", FALSE }
  };
