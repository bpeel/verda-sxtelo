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
#define W (VSX_TILE_SIZE + VSX_TILE_GAP)
#define H (VSX_TILE_SIZE + VSX_TILE_GAP)

const VsxTile
vsx_tile_data[VSX_TILE_DATA_N_TILES] =
  {
    { X+0*W,Y+0*H, -1, "A" }, { X+1*W,Y+0*H, -1, "A" },
    { X+2*W,Y+0*H, -1, "A" }, { X+3*W,Y+0*H, -1, "A" },
    { X+4*W,Y+0*H, -1, "A" }, { X+5*W,Y+0*H, -1, "A" },
    { X+6*W,Y+0*H, -1, "A" }, { X+7*W,Y+0*H, -1, "A" },
    { X+8*W,Y+0*H, -1, "A" }, { X+9*W,Y+0*H, -1, "B" },
    { X+10*W,Y+0*H, -1, "B" }, { X+11*W,Y+0*H, -1, "B" },
    { X+12*W,Y+0*H, -1, "C" }, { X+13*W,Y+0*H, -1, "C" },
    { X+14*W,Y+0*H, -1, "Ĉ" }, { X+15*W,Y+0*H, -1, "Ĉ" },
    { X+16*W,Y+0*H, -1, "D" },

    { X+0*W,Y+1*H, -1, "D" }, { X+1*W,Y+1*H, -1, "D" },
    { X+2*W,Y+1*H, -1, "E" }, { X+3*W,Y+1*H, -1, "E" },
    { X+4*W,Y+1*H, -1, "E" }, { X+5*W,Y+1*H, -1, "E" },
    { X+6*W,Y+1*H, -1, "E" }, { X+7*W,Y+1*H, -1, "E" },
    { X+8*W,Y+1*H, -1, "E" }, { X+9*W,Y+1*H, -1, "E" },
    { X+10*W,Y+1*H, -1, "F" }, { X+11*W,Y+1*H, -1, "F" },
    { X+12*W,Y+1*H, -1, "F" }, { X+13*W,Y+1*H, -1, "G" },
    { X+14*W,Y+1*H, -1, "G" }, { X+15*W,Y+1*H, -1, "Ĝ" },
    { X+16*W,Y+1*H, -1, "Ĝ" },

    { X+0*W,Y+2*H, -1, "H" }, { X+1*W,Y+2*H, -1, "H" },
    { X+2*W,Y+2*H, -1, "Ĥ" }, { X+3*W,Y+2*H, -1, "I" },
    { X+4*W,Y+2*H, -1, "I" }, { X+5*W,Y+2*H, -1, "I" },
    { X+6*W,Y+2*H, -1, "I" }, { X+7*W,Y+2*H, -1, "I" },
    { X+8*W,Y+2*H, -1, "I" }, { X+9*W,Y+2*H, -1, "I" },
    { X+10*W,Y+2*H, -1, "I" }, { X+11*W,Y+2*H, -1, "I" },
    { X+12*W,Y+2*H, -1, "I" }, { X+13*W,Y+2*H, -1, "J" },
    { X+14*W,Y+2*H, -1, "J" }, { X+15*W,Y+2*H, -1, "J" },
    { X+16*W,Y+2*H, -1, "J" },

    { X+0*W,Y+3*H, -1, "J" }, { X+1*W,Y+3*H, -1, "Ĵ" },
    { X+2*W,Y+3*H, -1, "K" }, { X+3*W,Y+3*H, -1, "K" },
    { X+4*W,Y+3*H, -1, "K" }, { X+5*W,Y+3*H, -1, "K" },
    { X+6*W,Y+3*H, -1, "K" }, { X+7*W,Y+3*H, -1, "K" },
    { X+8*W,Y+3*H, -1, "L" }, { X+9*W,Y+3*H, -1, "L" },
    { X+10*W,Y+3*H, -1, "L" }, { X+11*W,Y+3*H, -1, "L" },
    { X+12*W,Y+3*H, -1, "M" }, { X+13*W,Y+3*H, -1, "M" },
    { X+14*W,Y+3*H, -1, "M" }, { X+15*W,Y+3*H, -1, "M" },
    { X+16*W,Y+3*H, -1, "M" },

    { X+0*W,Y+4*H, -1, "M" }, { X+1*W,Y+4*H, -1, "N" },
    { X+2*W,Y+4*H, -1, "N" }, { X+3*W,Y+4*H, -1, "N" },
    { X+4*W,Y+4*H, -1, "N" }, { X+5*W,Y+4*H, -1, "N" },
    { X+6*W,Y+4*H, -1, "N" }, { X+7*W,Y+4*H, -1, "N" },
    { X+8*W,Y+4*H, -1, "N" }, { X+9*W,Y+4*H, -1, "O" },
    { X+10*W,Y+4*H, -1, "O" }, { X+11*W,Y+4*H, -1, "O" },
    { X+12*W,Y+4*H, -1, "O" }, { X+13*W,Y+4*H, -1, "O" },
    { X+14*W,Y+4*H, -1, "O" }, { X+15*W,Y+4*H, -1, "O" },
    { X+16*W,Y+4*H, -1, "O" },

    { X+0*W,Y+5*H, -1, "O" }, { X+1*W,Y+5*H, -1, "O" },
    { X+2*W,Y+5*H, -1, "O" }, { X+3*W,Y+5*H, -1, "P" },
    { X+4*W,Y+5*H, -1, "P" }, { X+5*W,Y+5*H, -1, "P" },
    { X+6*W,Y+5*H, -1, "P" }, { X+7*W,Y+5*H, -1, "P" },
    { X+8*W,Y+5*H, -1, "R" }, { X+9*W,Y+5*H, -1, "R" },
    { X+10*W,Y+5*H, -1, "R" }, { X+11*W,Y+5*H, -1, "R" },
    { X+12*W,Y+5*H, -1, "R" }, { X+13*W,Y+5*H, -1, "R" },
    { X+14*W,Y+5*H, -1, "R" }, { X+15*W,Y+5*H, -1, "S" },
    { X+16*W,Y+5*H, -1, "S" },

    { X+0*W,Y+6*H, -1, "S" }, { X+1*W,Y+6*H, -1, "S" },
    { X+2*W,Y+6*H, -1, "S" }, { X+3*W,Y+6*H, -1, "S" },
    { X+4*W,Y+6*H, -1, "S" }, { X+5*W,Y+6*H, -1, "Ŝ" },
    { X+6*W,Y+6*H, -1, "Ŝ" }, { X+7*W,Y+6*H, -1, "T" },
    { X+8*W,Y+6*H, -1, "T" }, { X+9*W,Y+6*H, -1, "T" },
    { X+10*W,Y+6*H, -1, "T" }, { X+11*W,Y+6*H, -1, "T" },
    { X+12*W,Y+6*H, -1, "U" }, { X+13*W,Y+6*H, -1, "U" },
    { X+14*W,Y+6*H, -1, "U" }, { X+15*W,Y+6*H, -1, "Ŭ" },
    { X+16*W,Y+6*H, -1, "Ŭ" },

    { X+0*W,Y+7*H, -1, "V" }, { X+1*W,Y+7*H, -1, "V" },
    { X+2*W,Y+7*H, -1, "Z" }
  };
