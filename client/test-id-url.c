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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include "vsx-util.h"

struct url_test {
        const char *url;
        uint64_t expected_value;
};

static const struct url_test
url_tests[] = {
        /* HTTP instead of HTTPS */
        { "http://gemelo.org/j/yv7K_sr-yvO", UINT64_C(0xcafecafecafecafe) },
        /* The URL part should be case insensitive */
        { "HTTPS://GEMELO.ORG/J/yv7K_sr-yvO", UINT64_C(0xcafecafecafecafe) },
};

static const char * const
invalid_url_tests[] = {
        /* Empty string */
        "",
        /* Bad protocol */
        "ftp://gemelo.org/j/yv7K_sr-yvO",
        /* Short protocol */
        "htt",
        /* Short URL part */
        "http://gemelo.o",
        /* Short ID part */
        "https://gemelo.org/j/AAAAAAAAAA",
        /* Last digit out of range */
        "https://gemelo.org/j/AAAAAAAAAAQ",
        /* Character just below digit range */
        "https://gemelo.org/j//AAAAAAAAAA",
        /* Character just below capital range */
        "https://gemelo.org/j/@AAAAAAAAAA",
        /* Character just below lower case range */
        "https://gemelo.org/j/`AAAAAAAAAA",
        /* Character just above lower case range */
        "https://gemelo.org/j/{AAAAAAAAAA",
        /* Character with MSB set */
        "https://gemelo.org/j/ĉAAAAAAAAA",
        /* Overly long ID part */
        "https://gemelo.org/j/AAAAAAAAAAAA",
};

static bool
check_url_expected(const char *url,
                   uint64_t expected_value)
{
        uint64_t decoded_value = ~expected_value;

        if (!vsx_id_url_decode(url, &decoded_value)) {
                fprintf(stderr,
                        "URL could not be decoded.\n"
                        " Expected value: 0x%" PRIx64 "\n"
                        " URL: %s\n",
                        expected_value,
                        url);
                return false;
        }

        if (expected_value != decoded_value) {
                fprintf(stderr,
                        "Decoded value does not match input value.\n"
                        " Input value: 0x%" PRIx64 "\n"
                        " URL: %s\n"
                        " Decoded value: 0x%" PRIx64 "\n",
                        expected_value,
                        url,
                        decoded_value);
                return false;
        }

        return true;
}

static bool
test_value(uint64_t input_value)
{
        char buf[VSX_ID_URL_ENCODED_SIZE + 16];

        memset(buf, 0x42, sizeof buf);

        vsx_id_url_encode(input_value, buf);

        if (buf[VSX_ID_URL_ENCODED_SIZE] != '\0') {
                fprintf(stderr,
                        "vsx_id_url_encode has not put the 0 terminator in the "
                        "right place.\n");
                return false;
        }

        for (int i = VSX_ID_URL_ENCODED_SIZE + 1; i < sizeof buf; i++) {
                if (buf[i] != 0x42) {
                        fprintf(stderr,
                                "vsx_id_url_encode has written past the end "
                                "of the buffer.\n");
                        return false;
                }
        }

        return check_url_expected(buf, input_value);
}

static bool
check_last_byte(void)
{
        bool ret = true;

        for (uint64_t id = 0; id < 0x100; id++) {
                if (!test_value(id))
                        ret = false;
        }

        return ret;
}

static bool
check_first_byte(void)
{
        bool ret = true;

        for (uint64_t id = 0; id < 0x100; id++) {
                if (!test_value(id << (64 - 8)))
                        ret = false;
        }

        return ret;
}

static bool
check_every_bit(void)
{
        bool ret = true;

        for (unsigned i = 0; i < sizeof (uint64_t) * 8; i++) {
                if (!test_value(UINT64_C(1) << i))
                        ret = false;
        }

        return ret;
}

static bool
test_urls(void)
{
        bool ret = true;

        for (unsigned i = 0; i < VSX_N_ELEMENTS(url_tests); i++) {
                if (!check_url_expected(url_tests[i].url,
                                        url_tests[i].expected_value))
                        ret = false;
        }

        return ret;
}

static bool
test_invalid_urls(void)
{
        bool ret = true;

        for (unsigned i = 0; i < VSX_N_ELEMENTS(invalid_url_tests); i++) {
                uint64_t decoded_value = 0;

                if (vsx_id_url_decode(invalid_url_tests[i], &decoded_value)) {
                        fprintf(stderr,
                                "URL decode unexpectedly succeeded.\n"
                                " URL:   %s\n"
                                " Value: 0x%" PRIx64 "\n",
                                invalid_url_tests[i],
                                decoded_value);
                        ret = false;
                }
        }

        return ret;
}

int
main(int argc, char **argv)
{
        int ret = EXIT_SUCCESS;

        if (!check_last_byte())
                ret = EXIT_FAILURE;
        if (!check_first_byte())
                ret = EXIT_FAILURE;
        if (!check_every_bit())
                ret = EXIT_FAILURE;
        if (!test_value(UINT64_MAX))
                ret = EXIT_FAILURE;
        if (!test_urls())
                ret = EXIT_FAILURE;
        if (!test_invalid_urls())
                ret = EXIT_FAILURE;

        return ret;
}
