/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2022, 2024  Neil Roberts
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

#include "vsx-generate-qr.h"

#include <string.h>
#include <stdint.h>
#include <assert.h>

#include "vsx-util.h"
#include "vsx-qr.h"
#include "vsx-id-url.h"

#include "crc-table.h"

#define INITIAL_CRC UINT32_MAX
#define INITIAL_FISCHER UINT32_C(1)

#define ZLIB_CMF 8

/* The number of bytes for the image in the PNG. This includes the
 * 1-byte filter header added to each scanline.
 */
#define N_IMAGE_BYTES ((VSX_QR_IMAGE_SIZE + 1) * VSX_QR_IMAGE_SIZE)

#define CHUNK_HEADER_SIZE (sizeof (uint32_t) * 2 + 4)

static const uint8_t
png_header[] =
{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
};

static const uint8_t ihdr_data[] = {
        0x00, 0x00, 0x00, VSX_QR_IMAGE_SIZE, /* width */
        0x00, 0x00, 0x00, VSX_QR_IMAGE_SIZE, /* height */
        8, /* bits per sample */
        0, /* color type (grayscale) */
        0, /* compression method (the only available one) */
        0, /* filter method */
        0, /* interlace method */
};

static const uint8_t zlib_header[] = {
        /* compression method / flags */
        ZLIB_CMF,
        /* check bits for CMF */
        31 - (ZLIB_CMF * 256 % 31),
        /* deflate block header (final block, no compression) */
        1,
        /* block len */
        N_IMAGE_BYTES & 0xff,
        N_IMAGE_BYTES >> 8,
        /* block nlen */
        (~N_IMAGE_BYTES) & 0xff,
        (~N_IMAGE_BYTES) >> 8,
};

#define IDAT_SIZE (sizeof zlib_header + N_IMAGE_BYTES + sizeof (uint32_t))

#define PNG_SIZE                                \
        (sizeof png_header +                    \
         CHUNK_HEADER_SIZE + sizeof ihdr_data + \
         CHUNK_HEADER_SIZE + IDAT_SIZE +        \
         CHUNK_HEADER_SIZE)

_Static_assert(PNG_SIZE == VSX_GENERATE_QR_PNG_SIZE,
               "PNG size declared in the header needs to match the "
               "calculated size");

struct chunk_writer {
        uint32_t crc;
        uint8_t *pos;
};

static uint32_t
update_crc(uint32_t crc,
           const uint8_t *buf,
           size_t len)
{
        for (size_t n = 0; n < len; n++)
                crc = crc_table[(crc ^ buf[n]) & 0xff] ^ (crc >> 8);

        return crc;
}

static uint32_t
update_fischer(uint32_t sums,
               const uint8_t *buf,
               size_t len)
{
        unsigned s1 = sums & 0xffff;
        unsigned s2 = sums >> 16;

        for (size_t n = 0; n < len; n++) {
                s1 = (s1 + buf[n]) % 65521;
                s2 = (s2 + s1) % 65521;
        }

        return (s2 << 16) | s1;
}

static void
write_data_no_crc(struct chunk_writer *writer,
                  const uint8_t *data,
                  size_t len)
{
        memcpy(writer->pos, data, len);
        writer->pos += len;
}

static void
write_data(struct chunk_writer *writer,
           const uint8_t *data,
           size_t len)
{
        writer->crc = update_crc(writer->crc, data, len);

        write_data_no_crc(writer, data, len);
}

static void
start_chunk(struct chunk_writer *writer,
            const char *type,
            uint32_t length)
{
        uint32_t length_be = VSX_UINT32_TO_BE(length);

        write_data_no_crc(writer,
                          (const uint8_t *) &length_be,
                          sizeof length_be);

        writer->crc = INITIAL_CRC;

        write_data(writer, (const uint8_t *) type, strlen(type));
}

static void
end_chunk(struct chunk_writer *writer)
{
        uint32_t final_crc = writer->crc ^ UINT32_MAX;
        uint32_t final_crc_be = VSX_UINT32_TO_BE(final_crc);

        write_data_no_crc(writer,
                          (const uint8_t *) &final_crc_be,
                          sizeof final_crc_be);
}

static void
write_ihdr(struct chunk_writer *writer)
{
        _Static_assert(VSX_QR_IMAGE_SIZE < 256,
                       "Image size needs to fit in a byte");

        start_chunk(writer, "IHDR", sizeof ihdr_data);
        write_data(writer, ihdr_data, sizeof ihdr_data);
        end_chunk(writer);
}

static void
write_idat(struct chunk_writer *writer,
           const uint8_t *image)
{
        start_chunk(writer, "IDAT", IDAT_SIZE);

        write_data(writer, zlib_header, sizeof zlib_header);

        uint32_t fischer = INITIAL_FISCHER;

        for (int y = 0; y < VSX_QR_IMAGE_SIZE; y++) {
                uint8_t filter = 0;

                fischer = update_fischer(fischer, &filter, sizeof filter);
                write_data(writer, &filter, sizeof filter);

                const uint8_t *scanline = image + y * VSX_QR_IMAGE_SIZE;

                fischer = update_fischer(fischer, scanline, VSX_QR_IMAGE_SIZE);
                write_data(writer, scanline, VSX_QR_IMAGE_SIZE);
        }

        uint32_t fischer_be = VSX_UINT32_TO_BE(fischer);

        write_data(writer, (const uint8_t *) &fischer_be, sizeof fischer_be);

        end_chunk(writer);
}

static void
write_iend(struct chunk_writer *writer)
{
        start_chunk(writer, "IEND", 0);
        end_chunk(writer);
}

static void
write_png(const uint8_t *image, uint8_t *png)
{
        struct chunk_writer writer = {
                .crc = 0,
                .pos = png,
        };

        write_data_no_crc(&writer, png_header, sizeof png_header);
        write_ihdr(&writer);
        write_idat(&writer, image);
        write_iend(&writer);

        assert(writer.pos - png == PNG_SIZE);
}

void
vsx_generate_qr(uint64_t id,
                uint8_t png[VSX_GENERATE_QR_PNG_SIZE])
{
        char url_buf[VSX_ID_URL_ENCODED_SIZE + 1];

        _Static_assert(sizeof url_buf == VSX_QR_DATA_SIZE + 1,
                       "QR data size and the invite URL size must be the same");

        vsx_id_url_encode(id, url_buf);

        uint8_t image[VSX_QR_IMAGE_SIZE * VSX_QR_IMAGE_SIZE];

        vsx_qr_create((const uint8_t *) url_buf, image);

        write_png(image, png);
}
