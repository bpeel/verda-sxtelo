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

#include "config.h"

#include "vsx-tile-data.h"

#include <string.h>

const VsxTileData
vsx_tile_data[VSX_TILE_DATA_N_ROOMS] =
  {
    {
      "eo",
      "AAAAAAAAABBBCCĈĈDDDEEEEEEEEFFFGGĜĜHHĤIIIIIIIIIIJJJJJĴKKKKKKLL"
      "LLMMMMMMNNNNNNNNOOOOOOOOOOOPPPPPRRRRRRRSSSSSSSŜŜTTTTTUUUŬŬVVZ"
    },
    {
      "en",
      "AAAAAAAAABBCCDDDDEEEEEEEEEEEEFFGGGHHIIIIIIIIIJKLLLLMMNNNNNNOO"
      "OOOOOOPPQRRRRRRSSSSTTTTTTUUUUVVWWXYYZNILRUIATCNUENOIEBYOEVASS"
    },
    {
      "fr",
      "AAAAAAAAABBCCDDDEEEEEEEEEEEEEEEFFGGHHIIIIIIIIJKLLLLLMMMNNNNNN"
      "OOOOOOPPQRRRRRRSSSSSSTTTTTTUUUUUUVVWXYZ"
      "IRTEOAUTUTAFNSUFLURCIT"
    },
    {
      "en-sv",
      "𐑠𐑶𐑾𐑽𐑭𐑘𐑺𐑔𐑷𐑸𐑫𐑬𐑡𐑗𐑿𐑵𐑹𐑻𐑜𐑙𐑖𐑴𐑳𐑳𐑣𐑣𐑱𐑱𐑲𐑲𐑓𐑓𐑪𐑪𐑼𐑼𐑚𐑚𐑰𐑰𐑢𐑢𐑢𐑨𐑨𐑨𐑝𐑝𐑝𐑐𐑐𐑐𐑧𐑧𐑧𐑮𐑮𐑮𐑥𐑥𐑥"
      "𐑟𐑟𐑟𐑟𐑒𐑒𐑒𐑒𐑞𐑞𐑞𐑞𐑛𐑛𐑛𐑛𐑤𐑤𐑤𐑤𐑤𐑕𐑕𐑕𐑕𐑕𐑕𐑩𐑩𐑩𐑩𐑩𐑩𐑩𐑑𐑑𐑑𐑑𐑑𐑑𐑑𐑑𐑑𐑯𐑯𐑯𐑯𐑯𐑯𐑯𐑯𐑦𐑦𐑦𐑦𐑦𐑦𐑦𐑦𐑦𐑦"
    },
  };

const VsxTileData *
vsx_tile_data_get_for_language_code (const char *language_code)
{
  for (int i = 0; i < VSX_TILE_DATA_N_ROOMS; i++)
    {
      if (!strcmp (vsx_tile_data[i].language_code, language_code))
        return vsx_tile_data + i;
    }

  return NULL;
}
