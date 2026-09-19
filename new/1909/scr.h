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

/*
 * ZX -> Atari PAL reference conversion.
 *
 * Returns 1 on success.
 * Returns 0 on conversion failure.
 *
 * The output is a 384x192 RGB image:
 *
 *   256x192 ZX
 *        ->
 *   192x192 logical Atari
 *        ->
 *   384x192 physical/reference image
 *
 * Every logical Atari pixel is duplicated horizontally.
 *
 * This version uses a fixed 16-entry ZX -> GTIA mapping.
 */
int zx_to_atari_color_rgb(const uint8_t *scr,
                          uint8_t *rgb);

/*
 * ZX -> Atari PAL reference conversion using
 * nearest GTIA color matching.
 *
 * The nearest color is calculated only once for
 * each of the 16 ZX palette entries.
 */
int zx_to_atari_color_rgb_nearest(const uint8_t *scr,
                                  uint8_t *rgb);

#endif