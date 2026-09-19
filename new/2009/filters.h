#ifndef FILTERS_H
#define FILTERS_H
#include <string.h>
#include <stdint.h>

#define SCREEN_WIDTH   256
#define SCREEN_HEIGHT  192
#define SCREEN_SIZE    6144
#define ZX_BITMAP_SIZE 6144

void convert_scr_to_atari_dither(
    const uint8_t *scr,
    uint8_t *screen,
    int dithering
);

void convert_scr_to_atari_ordered(
    const uint8_t *scr,
    uint8_t *screen
);

#endif