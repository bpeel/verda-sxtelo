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

#include "vsx-qr.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "vsx-util.h"

/* Code to generate a version 3 QR code with Q-level error-correction
 * in byte mode, giving exactly 32 bytes of data.
 */

#define N_MODULES 29
#define QUIET_ZONE_SIZE 4
#define ERROR_CORRECTION_CODEWORDS_PER_BLOCK 18
#define DATA_CODEWORDS_PER_BLOCK 17

_Static_assert(N_MODULES <= sizeof (uint32_t) * 8,
               "N_MODULES needs to fit in a uint32_t for vsx_qr_image");

_Static_assert(VSX_QR_IMAGE_SIZE == N_MODULES + QUIET_ZONE_SIZE * 2,
               "VSX_QR_IMAGE_SIZE needs to match the module size plus "
               "the quiet zone");

struct vsx_qr_image {
        /* One bit for each row of the image. Bit 0 is leftmost pixel.
         * Array index 0 is topmost.
         */
        uint32_t bits[N_MODULES];
};

#include "vsx-qr-data.h"

/* We always use Q-level correction. The only other thing left in the
 * format is the mask number. There’s only eight of them so we might
 * as well just hardcode the format with its correction bits instead
 * of trying to calculate it.
 */
static const uint16_t
format_bits_for_mask[] = {
        0x355f,
        0x3068,
        0x3f31,
        0x3a06,
        0x24b4,
        0x2183,
        0x2eda,
        0x2bed,
};

/* Lookup tables for log and exp operations used for calculating the
 * error-correction codewords. This is taken from here:
 *
 * https://dev.to/maxart2501/let-s-develop-a-qr-code-generator-part-iii-error-correction-1kbm
 */
static const uint8_t
coeff_log[] = {
        0, 0, 1, 25, 2, 50, 26, 198, 3, 223, 51, 238, 27, 104, 199, 75, 4, 100,
        224, 14, 52, 141, 239, 129, 28, 193, 105, 248, 200, 8, 76, 113, 5, 138,
        101, 47, 225, 36, 15, 33, 53, 147, 142, 218, 240, 18, 130, 69, 29, 181,
        194, 125, 106, 39, 249, 185, 201, 154, 9, 120, 77, 228, 114, 166, 6,
        191, 139, 98, 102, 221, 48, 253, 226, 152, 37, 179, 16, 145, 34, 136,
        54, 208, 148, 206, 143, 150, 219, 189, 241, 210, 19, 92, 131, 56, 70,
        64, 30, 66, 182, 163, 195, 72, 126, 110, 107, 58, 40, 84, 250, 133,
        186, 61, 202, 94, 155, 159, 10, 21, 121, 43, 78, 212, 229, 172, 115,
        243, 167, 87, 7, 112, 192, 247, 140, 128, 99, 13, 103, 74, 222, 237,
        49, 197, 254, 24, 227, 165, 153, 119, 38, 184, 180, 124, 17, 68, 146,
        217, 35, 32, 137, 46, 55, 63, 209, 91, 149, 188, 207, 205, 144, 135,
        151, 178, 220, 252, 190, 97, 242, 86, 211, 171, 20, 42, 93, 158, 132,
        60, 57, 83, 71, 109, 65, 162, 31, 45, 67, 216, 183, 123, 164, 118, 196,
        23, 73, 236, 127, 12, 111, 246, 108, 161, 59, 82, 41, 157, 85, 170,
        251, 96, 134, 177, 187, 204, 62, 90, 203, 89, 95, 176, 156, 169, 160,
        81, 11, 245, 22, 235, 122, 117, 44, 215, 79, 174, 213, 233, 230, 231,
        173, 232, 116, 214, 244, 234, 168, 80, 88, 175,
};

static const uint8_t
coeff_exp[] = {
        1, 2, 4, 8, 16, 32, 64, 128, 29, 58, 116, 232, 205, 135, 19, 38, 76,
        152, 45, 90, 180, 117, 234, 201, 143, 3, 6, 12, 24, 48, 96, 192, 157,
        39, 78, 156, 37, 74, 148, 53, 106, 212, 181, 119, 238, 193, 159, 35,
        70, 140, 5, 10, 20, 40, 80, 160, 93, 186, 105, 210, 185, 111, 222, 161,
        95, 190, 97, 194, 153, 47, 94, 188, 101, 202, 137, 15, 30, 60, 120,
        240, 253, 231, 211, 187, 107, 214, 177, 127, 254, 225, 223, 163, 91,
        182, 113, 226, 217, 175, 67, 134, 17, 34, 68, 136, 13, 26, 52, 104,
        208, 189, 103, 206, 129, 31, 62, 124, 248, 237, 199, 147, 59, 118, 236,
        197, 151, 51, 102, 204, 133, 23, 46, 92, 184, 109, 218, 169, 79, 158,
        33, 66, 132, 21, 42, 84, 168, 77, 154, 41, 82, 164, 85, 170, 73, 146,
        57, 114, 228, 213, 183, 115, 230, 209, 191, 99, 198, 145, 63, 126, 252,
        229, 215, 179, 123, 246, 241, 255, 227, 219, 171, 75, 150, 49, 98, 196,
        149, 55, 110, 220, 165, 87, 174, 65, 130, 25, 50, 100, 200, 141, 7, 14,
        28, 56, 112, 224, 221, 167, 83, 166, 81, 162, 89, 178, 121, 242, 249,
        239, 195, 155, 43, 86, 172, 69, 138, 9, 18, 36, 72, 144, 61, 122, 244,
        245, 247, 243, 251, 235, 203, 139, 11, 22, 44, 88, 176, 125, 250, 233,
        207, 131, 27, 54, 108, 216, 173, 71, 142, 0,
};

/* Taken from the table in the spec */
static const uint8_t
generator_poly[] = {
        1,
        239, /* coeff_exp[215] */
        251, /* coeff_exp[234] */
        183, /* coeff_exp[158] */
        113, /* coeff_exp[94] */
        149, /* coeff_exp[184] */
        175, /* coeff_exp[97] */
        199, /* coeff_exp[118] */
        215, /* coeff_exp[170] */
        240, /* coeff_exp[79] */
        220, /* coeff_exp[187] */
        73, /* coeff_exp[152] */
        82, /* coeff_exp[148] */
        173, /* coeff_exp[252] */
        75, /* coeff_exp[179] */
        32, /* coeff_exp[5] */
        67, /* coeff_exp[98] */
        217, /* coeff_exp[96] */
        146, /* coeff_exp[153] */
};

_Static_assert(VSX_N_ELEMENTS(generator_poly) ==
               ERROR_CORRECTION_CODEWORDS_PER_BLOCK + 1,
               "The generator polynomial needs to have a term for each "
               "error-correction codeword.");

static uint8_t
coeff_mul(uint8_t a, uint8_t b)
{
        return a && b ? coeff_exp[(coeff_log[a] + coeff_log[b]) % 255] : 0;
}

static uint8_t
coeff_div(uint8_t a, uint8_t b)
{
        return coeff_exp[(coeff_log[a] + coeff_log[b] * 254) % 255];
}

static void
poly_mul_by_generator(uint8_t poly2,
                      uint8_t *coeffs)
{
        for (int i = 0; i < VSX_N_ELEMENTS(generator_poly); i++)
                coeffs[i] = coeff_mul(generator_poly[i], poly2);
}

static void
get_error_correction_codewords(const uint8_t *data_codewords,
                               uint8_t *remainder_out)
{
        uint8_t remainder[DATA_CODEWORDS_PER_BLOCK +
                          ERROR_CORRECTION_CODEWORDS_PER_BLOCK];
        memcpy(remainder, data_codewords, DATA_CODEWORDS_PER_BLOCK);
        memset(remainder + DATA_CODEWORDS_PER_BLOCK,
               0,
               ERROR_CORRECTION_CODEWORDS_PER_BLOCK);

        for (int i = 0; i < DATA_CODEWORDS_PER_BLOCK; i++) {
                if (remainder[i] == 0)
                        continue;

                uint8_t factor = coeff_div(remainder[i],
                                           generator_poly[0]);

                uint8_t subst[VSX_N_ELEMENTS(generator_poly)];
                poly_mul_by_generator(factor, subst);

                for (int j = 1; j < VSX_N_ELEMENTS(subst); j++)
                        remainder[i + j] ^= subst[j];
        }

        memcpy(remainder_out,
               remainder + DATA_CODEWORDS_PER_BLOCK,
               ERROR_CORRECTION_CODEWORDS_PER_BLOCK);
}

static void
set_image_pixel(struct vsx_qr_image *image,
                int x,
                int y)
{
        image->bits[y] |= UINT32_C(1) << x;
}

static bool
check_pixel(const struct vsx_qr_image *image,
            int x,
            int y)
{
        return image->bits[y] & (UINT32_C(1) << x);
}

static void
store_format_bits(struct vsx_qr_image *image,
                  uint16_t bits)
{
        for (int i = 0; i < 8; i++) {
                if ((bits & (1 << i)) == 0)
                        continue;

                /* First eight bits are stored right-to-left below the
                 * top-right finder pattern.
                 */
                set_image_pixel(image, N_MODULES - 1 - i, 8);

                /* They are also stored top-to-bottom next to the
                 * topleft finder pattern, but with a gap for the
                 * timing pattern.
                 */
                int y = i >= 6 ? i + 1 : i;
                set_image_pixel(image, 8, y);
        }

        for (int i = 0; i < 7; i++) {
                if ((bits & (1 << (i + 8))) == 0)
                        continue;

                /* Upper seven bits are stored top-to-bottom next to
                 * the bottom-left finder pattern, with the top module
                 * reserved as the ominous “dark module”.
                 */
                set_image_pixel(image, 8, N_MODULES - 7 + i);

                /* They are also stored right-to-left below the
                 * topleft finder pattern with a gap for the timing
                 * pattern.
                 */
                int x = 7 - i;
                if (i >= 1)
                        x--;

                set_image_pixel(image, x, 8);
        }
}

static void
generate_pixel_image(const struct vsx_qr_image *image,
                     uint8_t *image_out)
{
        /* Initialise the image to white */
        memset(image_out, 255, VSX_QR_IMAGE_SIZE * VSX_QR_IMAGE_SIZE);

        for (int y = 0; y < N_MODULES; y++) {
                for (int x = 0; x < N_MODULES; x++) {
                        if (!check_pixel(image, x, y))
                                continue;

                        image_out[(y + QUIET_ZONE_SIZE) * VSX_QR_IMAGE_SIZE +
                                  x + QUIET_ZONE_SIZE] = 0;
                }
        }
}

struct bit_writer {
        int x, y;
        bool upwards;
        bool right;
};

static void
bit_writer_next_pos(struct bit_writer *writer)
{
        bool right = writer->right;
        writer->right = !right;

        /* If we are on the right-hand side of the column then just
         * move to the left.
         */
        if (right) {
                writer->x--;
                return;
        }

        /* Move back to the right */
        writer->x++;

        if (writer->upwards) {
                if (writer->y <= 0) {
                        writer->upwards = false;
                        writer->x -= 2;
                        if (writer->x == 6) {
                                /* If the right-hand side of the
                                 * column is in the vertical timing
                                 * pattern, then move the whole column
                                 * to the right instead of putting
                                 * only the left-hand side of the
                                 * column. The spec doesn’t seem to
                                 * clearly say that this is what
                                 * happens but it does seem to match
                                 * the pictures.
                                 */
                                writer->x--;
                        }
                } else {
                        writer->y--;
                }
        } else {
                if (writer->y >= N_MODULES - 1) {
                        writer->upwards = true;
                        writer->x -= 2;
                } else {
                        writer->y++;
                }
        }
}

static void
bit_writer_next_available_pos(struct bit_writer *writer)
{
        do {
                bit_writer_next_pos(writer);
        } while (!check_pixel(&data_mask_image, writer->x, writer->y));
}

static void
bit_writer_init(struct bit_writer *writer)
{
        /* Writing a bit starts by moving to the next available
         * position so we’ll start off the edge of the image.
         */
        writer->x = N_MODULES - 2;
        writer->y = N_MODULES;
        writer->upwards = true;
        writer->right = false;
}

static void
bit_writer_write_codeword(struct bit_writer *writer,
                          struct vsx_qr_image *image,
                          uint8_t codeword)
{
        for (unsigned i = 0; i < 8; i++) {
                bit_writer_next_available_pos(writer);

                if ((codeword & 0x80))
                        set_image_pixel(image, writer->x, writer->y);

                codeword <<= 1;
        }
}

static void
apply_mask(struct vsx_qr_image *image,
           const struct vsx_qr_image *mask)
{
        for (int i = 0; i < N_MODULES; i++)
                image->bits[i] ^= mask->bits[i];
}

void
vsx_qr_create(const uint8_t *data,
              uint8_t *image_out)
{
        struct vsx_qr_image image = base_image;

        int mask_num = 3;

        store_format_bits(&image, format_bits_for_mask[mask_num]);

        uint8_t block1_data[DATA_CODEWORDS_PER_BLOCK];
        /* Mode indicator is always 0b0100, ie, byte mode */
        block1_data[0] = 0x40 | (VSX_QR_DATA_SIZE >> 4);
        block1_data[1] = ((VSX_QR_DATA_SIZE & 0x0f) << 4) | (data[0] >> 4);

        for (int i = 2; i < VSX_N_ELEMENTS(block1_data); i++) {
                block1_data[i] = (((data[i - 2] & 0x0f) << 4) |
                                  (data[i - 1] >> 4));
        }

        uint8_t block2_data[DATA_CODEWORDS_PER_BLOCK];

        for (int i = 0; i < DATA_CODEWORDS_PER_BLOCK - 1; i++) {
                /* 1.5 codewords from the first block were used for
                 * something other than the data
                 */
                int data_index = i + DATA_CODEWORDS_PER_BLOCK - 2;
                block2_data[i] = (((data[data_index] & 0x0f) << 4) |
                                  (data[data_index + 1] >> 4));
        }
        /* Last codeword contains the last four bits of the data +
         * four zero bits for the terminator.
         */
        block2_data[DATA_CODEWORDS_PER_BLOCK - 1] =
                (data[VSX_QR_DATA_SIZE - 1] & 0x0f) << 4;

        uint8_t block1_ec[ERROR_CORRECTION_CODEWORDS_PER_BLOCK];
        get_error_correction_codewords(block1_data, block1_ec);

        uint8_t block2_ec[ERROR_CORRECTION_CODEWORDS_PER_BLOCK];
        get_error_correction_codewords(block2_data, block2_ec);

        struct bit_writer writer;

        bit_writer_init(&writer);

        for (int i = 0; i < DATA_CODEWORDS_PER_BLOCK; i++) {
                bit_writer_write_codeword(&writer,
                                          &image,
                                          block1_data[i]);
                bit_writer_write_codeword(&writer,
                                          &image,
                                          block2_data[i]);
        }

        for (int i = 0; i < ERROR_CORRECTION_CODEWORDS_PER_BLOCK; i++) {
                bit_writer_write_codeword(&writer,
                                          &image,
                                          block1_ec[i]);
                bit_writer_write_codeword(&writer,
                                          &image,
                                          block2_ec[i]);
        }

        apply_mask(&image, mask_images + mask_num);

        generate_pixel_image(&image, image_out);
}
