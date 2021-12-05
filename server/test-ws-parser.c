/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2021  Neil Roberts
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

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include "vsx-ws-parser.h"

typedef struct
{
  const char *headers;
  VsxWsParserError expected_code;
  const char *expected_message;
} ErrorTest;

static const ErrorTest
error_tests[] =
  {
    {
      "GET / HTTP/1.1\r\n"
      "\r\n",
      VSX_WS_PARSER_ERROR_INVALID,
      "Client sent a WebSocket header without a Sec-WebSocket-Key header",
    },
    {
      "GET / HTTP/1.1\r\n"
      "Sec-WebSocket-Key: potato\r\n"
      "Sec-WebSocket-Key: another-potato\r\n"
      "\r\n",
      VSX_WS_PARSER_ERROR_INVALID,
      "Client sent a WebSocket header with multiple Sec-WebSocket-Key headers",
    },
    {
      "GET\r\n",
      VSX_WS_PARSER_ERROR_INVALID,
      "Invalid HTTP request received",
    },
    {
      "GET /\r\n",
      VSX_WS_PARSER_ERROR_INVALID,
      "Invalid HTTP request received",
    },
    {
      "GET / HTTP\r\n",
      VSX_WS_PARSER_ERROR_UNSUPPORTED,
      "Unsupported HTTP version",
    },
    {
      "GET / FTTP/1.1\r\n",
      VSX_WS_PARSER_ERROR_UNSUPPORTED,
      "Unsupported HTTP version",
    },
    {
      "GET / HTTP/2\r\n",
      VSX_WS_PARSER_ERROR_UNSUPPORTED,
      "Unsupported HTTP version",
    },
    {
      "GET / HTTP/1.a\r\n",
      VSX_WS_PARSER_ERROR_UNSUPPORTED,
      "Unsupported HTTP version",
    },
    {
      "GET / HTTP/1.1\r\n"
      "Really-a-lot-of-data: "
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaa\r\n",
      VSX_WS_PARSER_ERROR_UNSUPPORTED,
      "Unsupported line length in HTTP request"
    },
    {
      "GET / HTTP/1.1\r\n"
      "Really-a-lot-of-data: "
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaa",
      VSX_WS_PARSER_ERROR_UNSUPPORTED,
      "Unsupported line length in HTTP request"
    },
    {
      "GET / HTTP/1.1"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaa\r\n",
      VSX_WS_PARSER_ERROR_UNSUPPORTED,
      "Unsupported line length in HTTP request"
    },
    {
      "GET / HTTP/1.1"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaa",
      VSX_WS_PARSER_ERROR_UNSUPPORTED,
      "Unsupported line length in HTTP request"
    },
    {
      "GET / HTTP/1.1\r\n"
      "Forgot-the-colon\r\n"
      "Another-header: great\r\n",
      VSX_WS_PARSER_ERROR_INVALID,
      "Invalid HTTP request received"
    },
    {
      "GET / HT\rTP/1.1\r\n",
      VSX_WS_PARSER_ERROR_UNSUPPORTED,
      "Unsupported HTTP version"
    },
    {
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "\ra",
      VSX_WS_PARSER_ERROR_UNSUPPORTED,
      "Unsupported line length in HTTP request"
    },
    {
      "GET / HTTP/1.1\r\n"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "\ra",
      VSX_WS_PARSER_ERROR_UNSUPPORTED,
      "Unsupported line length in HTTP request"
    },
  };

typedef struct
{
  const char *headers;
  const char *expected_hash;
} SuccessTest;

static const SuccessTest
success_tests[] =
  {
    {
      "GET / HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Sec-WebSocket-Key: potato\r\n"
      "\r\n"
      "TRAILING_DATA",
      "a783d7ed98e3e43c8954206bb78f70c51e11ca84"
    },
    {
      "GET / HTTP/1.1\r\n"
      "Sec-WebSocket-Key: pot\r\n"
      " ato\r\n"
      "\r\n"
      "TRAILING_DATA",
      "d5342d63046d2c434ade6caa65932eb6985599f9"
    },
    {
      "GET / HTTP/1.1\r\n"
      "Sec-WebSocket-Key: \r\n"
      "\r\n"
      "TRAILING_DATA",
      "29f87d408b0c559725eb110f6313c7cd6f1267cc"
    },
    {
      "\r\n"
      "GET / HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Sec-WebSocket-Key: potato\r\n"
      "\r\n"
      "TRAILING_DATA",
      "a783d7ed98e3e43c8954206bb78f70c51e11ca84"
    },
    {
      "\r\n"
      "GET / HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Sec-WebSocket-Key: pot\rato\r\n"
      "\r\n"
      "TRAILING_DATA",
      "f4ee6058a0f77a070538507d91fe15237717246c"
    },
  };

static bool
test_errors (void)
{
  bool ret = true;

  for (int i = 0; i < G_N_ELEMENTS (error_tests); i++)
    {
      VsxWsParser *parser = vsx_ws_parser_new ();

      size_t consumed;
      GError *error = NULL;

      VsxWsParserResult res =
        vsx_ws_parser_parse_data (parser,
                                  (const uint8_t *) error_tests[i].headers,
                                  strlen (error_tests[i].headers),
                                  &consumed,
                                  &error);

      if (res == VSX_WS_PARSER_RESULT_ERROR)
        {
          if (error->domain != VSX_WS_PARSER_ERROR)
            {
              fprintf (stderr,
                       "error test %i: error was from a different domain\n",
                       i);
              ret = false;
            }
          if (error->code != error_tests[i].expected_code)
            {
              fprintf (stderr,
                       "error test %i: expected code %i but received %i\n",
                       i,
                       (int) error->code,
                       (int) error_tests[i].expected_code);
              ret = false;
            }
          if (strcmp (error->message, error_tests[i].expected_message))
            {
              fprintf (stderr,
                       "error test %i: error message different\n"
                       "  Expected: %s\n"
                       "  Received: %s\n",
                       i,
                       error_tests[i].expected_message,
                       error->message);
              ret = false;
            }

          g_error_free (error);
        }
      else
        {
          fprintf (stderr,
                   "error test %i: expected failure but result was %i\n",
                   i,
                   (int) res);
          ret = false;
        }

      vsx_ws_parser_free (parser);
    }

  return ret;
}

static bool
compare_key_hash (const uint8_t *key_hash,
                  size_t key_hash_length,
                  const char *expected)
{
  while (*expected)
    {
      if (expected[1] == '\0')
        return false;

      if (key_hash_length <= 0)
        return false;

      int byte_value = ((g_ascii_xdigit_value (expected[0]) << 4) |
                        g_ascii_xdigit_value (expected[1]));

      if (byte_value != *key_hash)
        return false;

      expected += 2;
      key_hash_length--;
      key_hash++;
    }

  return key_hash_length == 0;
}

static void
dump_key_hash (const uint8_t *key_hash,
               size_t key_hash_length)
{
  for (unsigned i = 0; i < key_hash_length; i++)
    fprintf (stderr, "%02x", key_hash[i]);
}

static VsxWsParserResult
parse_data_byte_at_a_time (VsxWsParser *parser,
                           const uint8_t *data,
                           size_t length,
                           size_t *consumed_out,
                           GError **error)
{
  size_t total_consumed = 0;

  while (total_consumed < length)
    {
      size_t consumed;
      uint8_t bytes[] = { data[total_consumed], 0xff, 0xff, 0xff };

      switch (vsx_ws_parser_parse_data (parser,
                                        bytes,
                                        1, /* length */
                                        &consumed,
                                        error))
        {
        case VSX_WS_PARSER_RESULT_NEED_MORE_DATA:
          total_consumed++;
          break;
        case VSX_WS_PARSER_RESULT_FINISHED:
          total_consumed += consumed;
          *consumed_out = total_consumed;
          return VSX_WS_PARSER_RESULT_FINISHED;
        case VSX_WS_PARSER_RESULT_ERROR:
          return VSX_WS_PARSER_RESULT_ERROR;
        }
    }

  return VSX_WS_PARSER_RESULT_NEED_MORE_DATA;
}

static bool
test_success (bool byte_at_a_time)
{
  bool ret = true;

  for (int i = 0; i < G_N_ELEMENTS (success_tests); i++)
    {
      VsxWsParser *parser = vsx_ws_parser_new ();

      size_t consumed;
      GError *error = NULL;

      size_t headers_length = strlen (success_tests[i].headers);

      VsxWsParserResult res;

      if (byte_at_a_time)
        {
          res = parse_data_byte_at_a_time (parser,
                                           (const uint8_t *)
                                           success_tests[i].headers,
                                           headers_length,
                                           &consumed,
                                           &error);
        }
      else
        {
          res = vsx_ws_parser_parse_data (parser,
                                          (const uint8_t *)
                                          success_tests[i].headers,
                                          headers_length,
                                          &consumed,
                                          &error);
        }

      if (res == VSX_WS_PARSER_RESULT_FINISHED)
        {
          if (consumed > headers_length)
            {
              fprintf (stderr,
                       "success test %i: consumed > headers_length "
                       "(%zu > %zu)\n",
                       i,
                       consumed,
                       headers_length);
              ret = false;
            }
          else if (strcmp ("TRAILING_DATA",
                           success_tests[i].headers + consumed))
            {
              fprintf (stderr,
                       "success test %i: didn’t consume until TRAILING_DATA "
                       "(consumed = %zu)\n",
                       i,
                       consumed);
              ret = false;
            }

          size_t key_hash_length;
          const uint8_t *key_hash =
            vsx_ws_parser_get_key_hash (parser,
                                        &key_hash_length);

          if (!compare_key_hash (key_hash, key_hash_length,
                                 success_tests[i].expected_hash))
            {
              fprintf (stderr,
                       "success test %i: key hash does not match\n"
                       " Expected: %s\n"
                       " Received: ",
                       i,
                       success_tests[i].expected_hash);
              dump_key_hash (key_hash, key_hash_length);
              fputc ('\n', stderr);
              ret = false;
            }
        }
      else
        {
          fprintf (stderr,
                   "success test %i: expected success but result was %i\n",
                   i,
                   (int) res);
          if (res == VSX_WS_PARSER_RESULT_ERROR)
            {
              fprintf (stderr,
                       " error: %s\n",
                       error->message);
              g_error_free (error);
            }
          ret = false;
        }

      vsx_ws_parser_free (parser);
    }

  return ret;
}

int
main (int argc, char **argv)
{
  int ret = EXIT_SUCCESS;

  if (!test_errors ())
    ret = EXIT_FAILURE;

  if (test_success (false))
    {
      if (!test_success (true))
        ret = EXIT_FAILURE;
    }
  else
    {
      ret = EXIT_FAILURE;
    }

  return ret;
}
