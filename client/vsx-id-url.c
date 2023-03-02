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

#include "config.h"

#include "vsx-id-url.h"

#include <string.h>

#include "vsx-util.h"

/* These functions encode a 64-bit ID to a URL. The end of the URL
 * contains 11 characters of URL-friendly base64. The ‘+’ is replaced
 * with ‘-’ and the ‘/’ is replaced with ‘_’. The base64 padding
 * characters are not added because the size of the data is known so
 * the decoder can just ignore the extra bits.
 *
 * The URLs look like this:
 *
 *  https://gemelo.org/j/yv7K_sr-yvO
 *
 * to encode 0xcafecafecafecafe
 */

#define BITS_PER_CHAR 6
#define BITS_PER_ID (sizeof (uint64_t) * 8)
/* The number of bits used from the last character */
#define LAST_CHAR_BITS (BITS_PER_ID % BITS_PER_CHAR)

#define REST_URL "://gemelo.org/j/"
#define URL_PREFIX "https" REST_URL

_Static_assert(VSX_ID_URL_ENCODED_SIZE ==
               (sizeof URL_PREFIX) - 1 +
               BITS_PER_ID / BITS_PER_CHAR + !!LAST_CHAR_BITS,
               "VSX_ID_URL_ENCODED_SIZE must match the actual size");

static int
get_char_value(char ch)
{
        if (ch == '-')
                return 62;
        else if (ch < '0')
                return -1;
        else if (ch <= '9')
                return ch - '0' + 52;
        else if (ch < 'A')
                return -1;
        else if (ch <= 'Z')
                return ch - 'A';
        else if (ch == '_')
                return 63;
        else if (ch < 'a')
                return -1;
        else if (ch <= 'z')
                return ch - 'a' + 26;
        else
                return -1;
}

static bool
parse_id_part(const char *str,
              uint64_t *id_out)
{
        uint64_t id = 0;

        for (int i = 0; i < BITS_PER_ID / BITS_PER_CHAR; i++) {
                int char_value = get_char_value(*(str++));

                if (char_value == -1)
                        return false;

                id = (id << BITS_PER_CHAR) | char_value;
        }

        if (LAST_CHAR_BITS > 0) {
                int char_value = get_char_value(*(str++));

                if (char_value == -1 ||
                    char_value >= (1 << LAST_CHAR_BITS))
                        return false;

                id = (id << LAST_CHAR_BITS) | char_value;
        }

        if (*str != '\0')
                return false;

        *id_out = id;

        return true;
}

static bool
looking_at_no_case(const char *prefix,
                   const char *str)
{
        for (int i = 0; prefix[i]; i++) {
                if (vsx_ascii_tolower(prefix[i]) !=
                    vsx_ascii_tolower(str[i]))
                        return false;
        }

        return true;
}

bool
vsx_id_url_decode(const char *url,
                  uint64_t *id_out)
{
        static const char protocol[] = "http";

        if (!looking_at_no_case(protocol, url))
                return false;

        url += sizeof protocol - 1;

        /* Allow HTTPS as well */
        if (vsx_ascii_tolower(*url) == 's')
                url++;

        if (!looking_at_no_case(REST_URL, url))
                return false;

        url += (sizeof REST_URL) - 1;

        return parse_id_part(url, id_out);
}

static char
encode_to_char(unsigned value)
{
        if (value < 26)
                return value + 'A';
        else if (value < 52)
                return value - 26 + 'a';
        else if (value < 62)
                return value - 52 + '0';
        else if (value == 62)
                return '-';
        else
                return '_';
}

static void
encode_id_part(uint64_t id, char *str)
{
        for (int i = 0; i < BITS_PER_ID / BITS_PER_CHAR; i++) {
                *(str++) = encode_to_char(id >> (BITS_PER_ID - BITS_PER_CHAR));
                id <<= BITS_PER_CHAR;
        }

        if (LAST_CHAR_BITS > 0)
                *(str++) = encode_to_char(id >> (BITS_PER_ID - LAST_CHAR_BITS));

        *str = '\0';
}

void
vsx_id_url_encode(uint64_t id,
                  char *url)
{
        memcpy(url, URL_PREFIX, (sizeof URL_PREFIX) - 1);
        url += (sizeof URL_PREFIX) - 1;
        encode_id_part(id, url);
}
