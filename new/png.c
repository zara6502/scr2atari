#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "scr.h"
#include "png.h"

/* ========================================================= */
/* PNG writer                                                 */
/* ========================================================= */

uint32_t crc32_update(uint32_t crc, uint8_t c)
{
    unsigned k;

    crc ^= c;

    for (k = 0; k < 8; ++k)
    {
        if (crc & 1)
            crc = (crc >> 1) ^ 0xEDB88320U;
        else
            crc >>= 1;
    }

    return crc;
}

uint32_t adler32(const uint8_t *data, size_t size)
{
    uint32_t a = 1;
    uint32_t b = 0;
    size_t i;

    for (i = 0; i < size; ++i)
    {
        a += data[i];

        if (a >= 65521)
            a -= 65521;

        b += a;

        if (b >= 65521)
            b -= 65521;
    }

    return (b << 16) | a;
}

void write_be32(FILE *f, uint32_t v)
{
    fputc((int)((v >> 24) & 255), f);
    fputc((int)((v >> 16) & 255), f);
    fputc((int)((v >> 8) & 255), f);
    fputc((int)(v & 255), f);
}

int write_png_chunk(FILE *f,
                           const char *type,
                           const uint8_t *data,
                           size_t size)
{
    uint32_t crc;
    unsigned i;

    write_be32(f, (uint32_t)size);

    if (fwrite(type, 1, 4, f) != 4)
        return 0;

    if (size != 0 && fwrite(data, 1, size, f) != size)
        return 0;

    crc = 0xFFFFFFFFU;

    for (i = 0; i < 4; ++i)
        crc = crc32_update(crc, (uint8_t)type[i]);

    for (i = 0; i < size; ++i)
        crc = crc32_update(crc, data[i]);

    crc ^= 0xFFFFFFFFU;

    write_be32(f, crc);

    return !ferror(f);
}

int write_png(const char *filename, const uint8_t *rgb)
{
    FILE *f;
    uint8_t *raw;
    uint8_t *zlib;
    size_t raw_size;
    size_t zlib_size;
    size_t pos;
    size_t i;
    uint32_t sum;

    raw_size =
        (size_t)SCREEN_HEIGHT *
        (1 + SCREEN_WIDTH * 3);

    raw = (uint8_t *)malloc(raw_size);

    if (!raw)
        return 0;

    pos = 0;

    for (i = 0; i < SCREEN_HEIGHT; ++i)
    {
        raw[pos++] = 0;

        memcpy(raw + pos,
               rgb + i * SCREEN_WIDTH * 3,
               SCREEN_WIDTH * 3);

        pos += SCREEN_WIDTH * 3;
    }

    zlib_size =
        raw_size +
        raw_size / 65535 * 5 +
        32;

    zlib = (uint8_t *)malloc(zlib_size);

    if (!zlib)
    {
        free(raw);
        return 0;
    }

    pos = 0;

    zlib[pos++] = 0x78;
    zlib[pos++] = 0x01;

    i = 0;

    while (i < raw_size)
    {
        size_t block_size;
        int final_block;

        block_size = raw_size - i;

        if (block_size > 65535)
            block_size = 65535;

        final_block =
            (i + block_size == raw_size);

        zlib[pos++] =
            (uint8_t)(final_block ? 1 : 0);

        zlib[pos++] =
            (uint8_t)(block_size & 255);

        zlib[pos++] =
            (uint8_t)((block_size >> 8) & 255);

        zlib[pos++] =
            (uint8_t)((~block_size) & 255);

        zlib[pos++] =
            (uint8_t)(((~block_size) >> 8) & 255);

        memcpy(zlib + pos,
               raw + i,
               block_size);

        pos += block_size;
        i += block_size;
    }

    sum = adler32(raw, raw_size);

    zlib[pos++] = (uint8_t)((sum >> 24) & 255);
    zlib[pos++] = (uint8_t)((sum >> 16) & 255);
    zlib[pos++] = (uint8_t)((sum >> 8) & 255);
    zlib[pos++] = (uint8_t)(sum & 255);

    f = fopen(filename, "wb");

    if (!f)
    {
        free(zlib);
        free(raw);
        return 0;
    }

    {
        static const uint8_t signature[8] =
        {
            137, 80, 78, 71,
            13, 10, 26, 10
        };

        if (fwrite(signature, 1, 8, f) != 8)
        {
            fclose(f);
            free(zlib);
            free(raw);
            return 0;
        }
    }

    {
        uint8_t ihdr[13];

        ihdr[0] = 0;
        ihdr[1] = 0;
        ihdr[2] = 1;
        ihdr[3] = 0;

        ihdr[4] = 0;
        ihdr[5] = 0;
        ihdr[6] = 0;
        ihdr[7] = 192;

        ihdr[8]  = 8;
        ihdr[9]  = 2;
        ihdr[10] = 0;
        ihdr[11] = 0;
        ihdr[12] = 0;

        if (!write_png_chunk(f, "IHDR", ihdr, 13))
        {
            fclose(f);
            free(zlib);
            free(raw);
            return 0;
        }
    }

    if (!write_png_chunk(f, "IDAT", zlib, pos))
    {
        fclose(f);
        free(zlib);
        free(raw);
        return 0;
    }

    if (!write_png_chunk(f, "IEND", NULL, 0))
    {
        fclose(f);
        free(zlib);
        free(raw);
        return 0;
    }

    fclose(f);

    free(zlib);
    free(raw);

    return 1;
}
