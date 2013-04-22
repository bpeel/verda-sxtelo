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
    { X+0*W,Y+0*H, "A" }, { X+1*W,Y+0*H, "A" },
    { X+2*W,Y+0*H, "A" }, { X+3*W,Y+0*H, "A" },
    { X+4*W,Y+0*H, "A" }, { X+5*W,Y+0*H, "A" },
    { X+6*W,Y+0*H, "A" }, { X+7*W,Y+0*H, "A" },
    { X+8*W,Y+0*H, "A" }, { X+9*W,Y+0*H, "B" },
    { X+10*W,Y+0*H, "B" }, { X+11*W,Y+0*H, "B" },
    { X+12*W,Y+0*H, "C" }, { X+13*W,Y+0*H, "C" },
    { X+14*W,Y+0*H, "Ĉ" }, { X+15*W,Y+0*H, "Ĉ" },
    { X+16*W,Y+0*H, "D" },

    { X+0*W,Y+1*H, "D" }, { X+1*W,Y+1*H, "D" },
    { X+2*W,Y+1*H, "E" }, { X+3*W,Y+1*H, "E" },
    { X+4*W,Y+1*H, "E" }, { X+5*W,Y+1*H, "E" },
    { X+6*W,Y+1*H, "E" }, { X+7*W,Y+1*H, "E" },
    { X+8*W,Y+1*H, "E" }, { X+9*W,Y+1*H, "E" },
    { X+10*W,Y+1*H, "F" }, { X+11*W,Y+1*H, "F" },
    { X+12*W,Y+1*H, "F" }, { X+13*W,Y+1*H, "G" },
    { X+14*W,Y+1*H, "G" }, { X+15*W,Y+1*H, "Ĝ" },
    { X+16*W,Y+1*H, "Ĝ" },

    { X+0*W,Y+2*H, "H" }, { X+1*W,Y+2*H, "H" },
    { X+2*W,Y+2*H, "Ĥ" }, { X+3*W,Y+2*H, "I" },
    { X+4*W,Y+2*H, "I" }, { X+5*W,Y+2*H, "I" },
    { X+6*W,Y+2*H, "I" }, { X+7*W,Y+2*H, "I" },
    { X+8*W,Y+2*H, "I" }, { X+9*W,Y+2*H, "I" },
    { X+10*W,Y+2*H, "I" }, { X+11*W,Y+2*H, "I" },
    { X+12*W,Y+2*H, "I" }, { X+13*W,Y+2*H, "J" },
    { X+14*W,Y+2*H, "J" }, { X+15*W,Y+2*H, "J" },
    { X+16*W,Y+2*H, "J" },

    { X+0*W,Y+3*H, "J" }, { X+1*W,Y+3*H, "Ĵ" },
    { X+2*W,Y+3*H, "K" }, { X+3*W,Y+3*H, "K" },
    { X+4*W,Y+3*H, "K" }, { X+5*W,Y+3*H, "K" },
    { X+6*W,Y+3*H, "K" }, { X+7*W,Y+3*H, "K" },
    { X+8*W,Y+3*H, "L" }, { X+9*W,Y+3*H, "L" },
    { X+10*W,Y+3*H, "L" }, { X+11*W,Y+3*H, "L" },
    { X+12*W,Y+3*H, "M" }, { X+13*W,Y+3*H, "M" },
    { X+14*W,Y+3*H, "M" }, { X+15*W,Y+3*H, "M" },
    { X+16*W,Y+3*H, "M" },

    { X+0*W,Y+4*H, "M" }, { X+1*W,Y+4*H, "N" },
    { X+2*W,Y+4*H, "N" }, { X+3*W,Y+4*H, "N" },
    { X+4*W,Y+4*H, "N" }, { X+5*W,Y+4*H, "N" },
    { X+6*W,Y+4*H, "N" }, { X+7*W,Y+4*H, "N" },
    { X+8*W,Y+4*H, "N" }, { X+9*W,Y+4*H, "O" },
    { X+10*W,Y+4*H, "O" }, { X+11*W,Y+4*H, "O" },
    { X+12*W,Y+4*H, "O" }, { X+13*W,Y+4*H, "O" },
    { X+14*W,Y+4*H, "O" }, { X+15*W,Y+4*H, "O" },
    { X+16*W,Y+4*H, "O" },

    { X+0*W,Y+5*H, "O" }, { X+1*W,Y+5*H, "O" },
    { X+2*W,Y+5*H, "O" }, { X+3*W,Y+5*H, "P" },
    { X+4*W,Y+5*H, "P" }, { X+5*W,Y+5*H, "P" },
    { X+6*W,Y+5*H, "P" }, { X+7*W,Y+5*H, "P" },
    { X+8*W,Y+5*H, "R" }, { X+9*W,Y+5*H, "R" },
    { X+10*W,Y+5*H, "R" }, { X+11*W,Y+5*H, "R" },
    { X+12*W,Y+5*H, "R" }, { X+13*W,Y+5*H, "R" },
    { X+14*W,Y+5*H, "R" }, { X+15*W,Y+5*H, "S" },
    { X+16*W,Y+5*H, "S" },

    { X+0*W,Y+6*H, "S" }, { X+1*W,Y+6*H, "S" },
    { X+2*W,Y+6*H, "S" }, { X+3*W,Y+6*H, "S" },
    { X+4*W,Y+6*H, "S" }, { X+5*W,Y+6*H, "Ŝ" },
    { X+6*W,Y+6*H, "Ŝ" }, { X+7*W,Y+6*H, "T" },
    { X+8*W,Y+6*H, "T" }, { X+9*W,Y+6*H, "T" },
    { X+10*W,Y+6*H, "T" }, { X+11*W,Y+6*H, "T" },
    { X+12*W,Y+6*H, "U" }, { X+13*W,Y+6*H, "U" },
    { X+14*W,Y+6*H, "U" }, { X+15*W,Y+6*H, "Ŭ" },
    { X+16*W,Y+6*H, "Ŭ" },

    { X+0*W,Y+7*H, "V" }, { X+1*W,Y+7*H, "V" },
    { X+2*W,Y+7*H, "Z" }
  };
