#ifndef COLOR_H
#define COLOR_H

#include <stdint.h>

/*
 * Color reference geometry.
 *
 * Source ZX:
 *
 *     256 x 192
 *
 * Logical Atari:
 *
 *     160 x 240
 *
 * Physical RGB reference:
 *
 *     320 x 240
 *
 * Each logical Atari pixel is represented by
 * two horizontal RGB pixels.
 */
#define COLOR_LOGICAL_WIDTH    160
#define COLOR_LOGICAL_HEIGHT   240

#define COLOR_PHYSICAL_WIDTH   320
#define COLOR_PHYSICAL_HEIGHT  240

#define COLOR_SOURCE_WIDTH     256
#define COLOR_SOURCE_HEIGHT    192

#define COLOR_MAX_COLORS       4


/*
 * Resize mode.
 */
#define COLOR_RESIZE_NEAREST   0
#define COLOR_RESIZE_BRES      1
#define COLOR_RESIZE_AREA      2


/*
 * Convert ZX Spectrum SCR to Atari color reference RGB.
 *
 * nearest == 0:
 *     fixed ZX -> GTIA mapping (-color)
 *
 * nearest != 0:
 *     nearest GTIA color mapping (-colorn and variants)
 *
 * resize_mode:
 *
 *     COLOR_RESIZE_NEAREST
 *         nearest-neighbour
 *
 *     COLOR_RESIZE_BRES
 *         integer error-accumulation resize
 *
 *     COLOR_RESIZE_AREA
 *         area/box resampling
 *
 * Geometry:
 *
 *     256 x 192
 *          |
 *          v
 *     160 x 240
 *          |
 *          v
 *     320 x 240 RGB
 *
 * Returns:
 *
 *     1  conversion successful
 *     0  more than four final Atari colors
 *        or invalid input
 */
int convert_scr_to_color_reference(
    const uint8_t *scr,
    uint8_t *rgb,
    int nearest,
    int resize_mode,
    unsigned *unique_colors);

#endif