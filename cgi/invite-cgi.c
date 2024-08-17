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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "vsx-generate-qr.h"
#include "vsx-id-url.h"
#include "vsx-util.h"

static void
report_error(void)
{
        printf("400 Bad Request\r\n"
               "Content-Type: text/plain\r\n"
               "\r\n"
               "Invalid query string\r\n");
}

static bool
handle_query_string(void)
{
        const char *qs = getenv("QUERY_STRING");

        if (qs == NULL)
                return false;

        uint64_t id;

        if (!vsx_id_url_decode_id_part(qs, &id))
                return false;

        fputs("Content-Type: image/png\r\n"
              "\r\n",
              stdout);

        uint8_t *png = vsx_alloc(VSX_GENERATE_QR_PNG_SIZE);

        vsx_generate_qr(id, png);

        fwrite(png, 1, VSX_GENERATE_QR_PNG_SIZE, stdout);

        vsx_free(png);

        return true;
}

int
main(int argc, char **argv)
{
        if (!handle_query_string())
                report_error();

        return EXIT_SUCCESS;
}
