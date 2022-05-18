/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2022  Neil Roberts
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

#ifndef VSX_ID_URL_H
#define VSX_ID_URL_H

#include <stdbool.h>
#include <stdint.h>

/* String length of the encoded URL, not including the zero terminator */
#define VSX_ID_URL_ENCODED_SIZE 32

void
vsx_id_url_encode(uint64_t id,
                  char *url);

bool
vsx_id_url_decode_id_part(const char *str,
                          uint64_t *id_out);

bool
vsx_id_url_decode(const char *url,
                  uint64_t *id_out);

#endif /* VSX_ID_URL_H */
