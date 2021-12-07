/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2011 Intel Corporation.
 * Copyright (C) 2013 Neil Roberts
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

/* This file is borrowed from Cogl */

#ifndef __VSX_FLAGS_H
#define __VSX_FLAGS_H

#include <glib.h>
#include <string.h>

/* These are macros used to implement a fixed-size array of bits. This
   should be used instead of CoglBitmask when the maximum bit number
   that will be set is known at compile time, for example when setting
   for recording a set of known available features */

/* The bits are stored in an array of unsigned longs. To use these
   macros, you would typically have an enum defining the available
   bits with an extra last enum to define the maximum value. Then to
   store the flags you would declare an array of unsigned longs sized
   using VSX_FLAGS_N_LONGS_FOR_SIZE, eg:

   typedef enum { FEATURE_A, FEATURE_B, FEATURE_C, N_FEATURES } Features;

   unsigned long feature_flags[VSX_FLAGS_N_LONGS_FOR_SIZE (N_FEATURES)];
*/

#define VSX_FLAGS_N_LONGS_FOR_SIZE(size)        \
  (((size) +                                    \
    (sizeof (unsigned long) * 8 - 1))           \
   / (sizeof (unsigned long) * 8))

/* @flag is expected to be constant so these should result in a
   constant expression. This means that setting a flag is equivalent
   to just setting in a bit in a global variable at a known
   location */
#define VSX_FLAGS_GET_INDEX(flag)               \
  ((flag) / (sizeof (unsigned long) * 8))
#define VSX_FLAGS_GET_MASK(flag)                \
  (1UL << ((unsigned long) (flag) &             \
           (sizeof (unsigned long) * 8 - 1)))

#define VSX_FLAGS_GET(array, flag)              \
  (!!((array)[VSX_FLAGS_GET_INDEX (flag)] &     \
      VSX_FLAGS_GET_MASK (flag)))

/* The expectation here is that @value will be constant so the if
   statement will be optimised out */
#define VSX_FLAGS_SET(array, flag, value)       \
  do {                                          \
    if (value)                                  \
      ((array)[VSX_FLAGS_GET_INDEX (flag)] |=   \
       VSX_FLAGS_GET_MASK (flag));              \
    else                                        \
      ((array)[VSX_FLAGS_GET_INDEX (flag)] &=   \
       ~VSX_FLAGS_GET_MASK (flag));             \
  } while (0)

static inline void
vsx_flags_set_range (unsigned long *array,
                     int range)
{
  int i;

  for (i = 0; i < range / (sizeof (unsigned long) * 8); i++)
    array[i] = ~0UL;

  if (i * sizeof (unsigned long) * 8 < range)
    {
      int bits = range - i * sizeof (unsigned long) * 8;
      array[i] |= (1UL << bits) - 1;
    }
}

static inline int
vsx_flags_find_first_bit (const unsigned long *array)
{
  const unsigned long *p;

  for (p = array; *p == 0; p++);

  return ffsl (*p) - 1 + (p - array) * sizeof (unsigned long) * 8;
}

/* Macros to help iterate an array of flags. It should be used like
 * this:
 *
 * int n_longs = VSX_FLAGS_N_LONGS_FOR_SIZE (...);
 * unsigned long flags[n_longs];
 * int bit_num;
 *
 * VSX_FLAGS_FOREACH_START (flags, n_longs, bit_num)
 *   {
 *     do_something_with_the_bit (bit_num);
 *   }
 * VSX_FLAGS_FOREACH_END;
 */
#define VSX_FLAGS_FOREACH_START(array, n_longs, bit)    \
  do {                                                  \
  const unsigned long *_p = (array);                    \
  int _n_longs = (n_longs);                             \
  int _i;                                               \
                                                        \
  for (_i = 0; _i < _n_longs; _i++)                     \
    {                                                   \
  unsigned long _mask = *(_p++);                        \
                                                        \
  (bit) = _i * sizeof (unsigned long) * 8 - 1;          \
                                                        \
  while (_mask)                                         \
    {                                                   \
      int _next_bit = _cogl_util_ffsl (_mask);          \
      (bit) += _next_bit;                               \
      /* This odd two-part shift is to avoid */         \
      /* shifting by sizeof (long)*8 which has */       \
      /* undefined results according to the */          \
      /* C spec (and seems to be a no-op in */          \
      /* practice) */                                   \
      _mask = (_mask >> (_next_bit - 1)) >> 1;          \

#define VSX_FLAGS_FOREACH_END                   \
  } } } while (0)

#endif /* __VSX_FLAGS_H */
