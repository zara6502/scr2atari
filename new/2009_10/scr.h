#ifndef SCR_H
#define SCR_H

#include <stdint.h>

#define SCR_SIZE        6912
#define BITMAP_SIZE     6144
#define ATTR_SIZE       768

#define SCREEN_WIDTH    256
#define SCREEN_HEIGHT   192
#define SCREEN_SIZE     6144

#define ATARI_LOGICAL_WIDTH   192
#define ATARI_PHYSICAL_WIDTH  384
#define ATARI_COLOR_COUNT     256

void zx_get_palette(int index,
                    uint8_t *r,
                    uint8_t *g,
                    uint8_t *b);

int zx_color_is_light(unsigned color);

void zx_to_rgb(const uint8_t *scr,
               uint8_t *rgb);

void convert_scr_to_atari(const uint8_t *scr,
                          uint8_t *screen);

#endif