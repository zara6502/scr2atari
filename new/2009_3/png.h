#ifndef PNG_H
#define PNG_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

uint32_t crc32_update(uint32_t crc, uint8_t c);
uint32_t adler32(const uint8_t *data, size_t size);

void write_be32(FILE *f, uint32_t v);

int write_png_chunk(FILE *f,
                    const char *type,
                    const uint8_t *data,
                    size_t size);

int write_png(const char *filename,
              const uint8_t *rgb,
              unsigned width,
              unsigned height);

#endif