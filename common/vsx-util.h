/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2019  Neil Roberts
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

#ifndef VSX_UTIL_H
#define VSX_UTIL_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __GNUC__
#define VSX_NO_RETURN __attribute__((noreturn))
#define VSX_PRINTF_FORMAT(string_index, first_to_check) \
  __attribute__((format(printf, string_index, first_to_check)))
#define VSX_NULL_TERMINATED __attribute__((sentinel))
#else
#define VSX_PRINTF_FORMAT(string_index, first_to_check)
#define VSX_NULL_TERMINATED
#ifdef _MSC_VER
#define VSX_NO_RETURN __declspec(noreturn)
#else
#define VSX_NO_RETURN
#endif
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#define VSX_N_ELEMENTS(array) \
  (sizeof (array) / sizeof ((array)[0]))

#define VSX_SWAP_UINT16(x)                      \
  ((uint16_t)                                   \
   (((uint16_t) (x) >> 8) |                     \
    ((uint16_t) (x) << 8)))
#define VSX_SWAP_UINT32(x)                               \
  ((uint32_t)                                           \
   ((((uint32_t) (x) & UINT32_C (0x000000ff)) << 24) |  \
    (((uint32_t) (x) & UINT32_C (0x0000ff00)) << 8) |   \
    (((uint32_t) (x) & UINT32_C (0x00ff0000)) >> 8) |   \
    (((uint32_t) (x) & UINT32_C (0xff000000)) >> 24)))
#define VSX_SWAP_UINT64(x)                                               \
  ((uint64_t)                                                           \
   ((((uint64_t) (x) & (uint64_t) UINT64_C (0x00000000000000ff)) << 56) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0x000000000000ff00)) << 40) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0x0000000000ff0000)) << 24) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0x00000000ff000000)) << 8) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0x000000ff00000000)) >> 8) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0x0000ff0000000000)) >> 24) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0x00ff000000000000)) >> 40) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0xff00000000000000)) >> 56)))

#if defined(HAVE_BIG_ENDIAN)
#define VSX_INT16_FROM_BE(x) ((int16_t) (x))
#define VSX_UINT16_FROM_BE(x) ((uint16_t) (x))
#define VSX_UINT32_FROM_BE(x) ((uint32_t) (x))
#define VSX_UINT64_FROM_BE(x) ((uint64_t) (x))
#define VSX_INT16_FROM_LE(x) ((int16_t) VSX_SWAP_UINT16((uint16_t) (x)))
#define VSX_UINT16_FROM_LE(x) VSX_SWAP_UINT16((uint16_t) (x))
#define VSX_UINT32_FROM_LE(x) VSX_SWAP_UINT32((uint32_t) (x))
#define VSX_UINT64_FROM_LE(x) VSX_SWAP_UINT64((uint64_t) (x))
#elif defined(HAVE_LITTLE_ENDIAN)
#define VSX_INT16_FROM_LE(x) ((int16_t) (x))
#define VSX_UINT16_FROM_LE(x) ((uint16_t) (x))
#define VSX_UINT32_FROM_LE(x) ((uint32_t) (x))
#define VSX_UINT64_FROM_LE(x) ((uint64_t) (x))
#define VSX_INT16_FROM_BE(x) ((int16_t) VSX_SWAP_UINT16((uint16_t) (x)))
#define VSX_UINT16_FROM_BE(x) VSX_SWAP_UINT16((uint16_t) (x))
#define VSX_UINT32_FROM_BE(x) VSX_SWAP_UINT32((uint32_t) (x))
#define VSX_UINT64_FROM_BE(x) VSX_SWAP_UINT64((uint64_t) (x))
#else
#error Platform is neither little-endian nor big-endian
#endif

#define VSX_INT16_TO_LE(x) VSX_INT16_FROM_LE(x)
#define VSX_UINT16_TO_LE(x) VSX_UINT16_FROM_LE(x)
#define VSX_UINT16_TO_BE(x) VSX_UINT16_FROM_BE(x)
#define VSX_UINT32_TO_LE(x) VSX_UINT32_FROM_LE(x)
#define VSX_UINT32_TO_BE(x) VSX_UINT32_FROM_BE(x)
#define VSX_UINT64_TO_LE(x) VSX_UINT64_FROM_LE(x)
#define VSX_UINT64_TO_BE(x) VSX_UINT64_FROM_BE(x)

void *
vsx_alloc(size_t size);

void *
vsx_calloc(size_t size);

void *
vsx_realloc(void *ptr, size_t size);

void
vsx_free(void *ptr);

VSX_NULL_TERMINATED char *
vsx_strconcat(const char *string1, ...);

char *
vsx_strdup(const char *str);

char *
vsx_strndup(const char *str, size_t size);

void *
vsx_memdup(const void *data, size_t size);

VSX_NO_RETURN
VSX_PRINTF_FORMAT(1, 2)
void
vsx_fatal(const char *format, ...);

VSX_PRINTF_FORMAT(1, 2) void
vsx_warning(const char *format, ...);

int
vsx_close(int fd);

static inline char
vsx_ascii_tolower(char ch)
{
        if (ch >= 'A' && ch <= 'Z')
                return ch - 'A' + 'a';
        else
                return ch;
}

static inline bool
vsx_ascii_isdigit(char ch)
{
        return ch >= '0' && ch <= '9';
}

/* Returns true if the given strings are the same, ignoring case. The
 * case is compared ignoring the locale and operates on ASCII only.
 */
bool
vsx_ascii_string_case_equal(const char *a, const char *b);

#endif /* VSX_UTIL_H */
