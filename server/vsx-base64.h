/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2014, 2020  Neil Roberts
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

#ifndef VSX_BASE64_H
#define VSX_BASE64_H

#include <stdlib.h>
#include <stdint.h>

#include "vsx-error.h"

extern struct vsx_error_domain
vsx_base64_error;

enum vsx_base64_error {
        VSX_BASE64_ERROR_INVALID_PADDING
};

struct vsx_base64_data {
        int n_padding;
        int n_chars;
        int value;
};

#define VSX_BASE64_MAX_INPUT_FOR_SIZE(input_size)       \
        ((size_t) (input_size) * 4 / 3)
#define VSX_BASE64_ENCODED_SIZE(decoded_size)   \
        ((((decoded_size) + 2) / 3) * 4)

void
vsx_base64_decode_start(struct vsx_base64_data *data);

ssize_t
vsx_base64_decode(struct vsx_base64_data *data,
                  const uint8_t *in_buffer,
                  size_t length,
                  uint8_t *out_buffer,
                  struct vsx_error **error);

ssize_t
vsx_base64_decode_end(struct vsx_base64_data *data,
                      uint8_t *buffer,
                      struct vsx_error **error);

size_t
vsx_base64_encode(const uint8_t *data_in,
                  size_t data_in_length,
                  char *data_out);

#endif /* VSX_BASE64_H */
