#ifndef COLOR_H
#define COLOR_H

#include <stdint.h>

/*
 * Color reference geometry.
 *
 * Logical Atari image:
 *
 *     160 x 240
 *
 * Physical RGB image:
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

/*
 * Original ZX image occupies the central 192 lines.
 */
#define COLOR_TOP_BORDER       24
#define COLOR_BOTTOM_BORDER    24

#define COLOR_SOURCE_WIDTH     256
#define COLOR_SOURCE_HEIGHT    192

/*
 * Current color mode supports a maximum of four
 * distinct final Atari colors.
 */
#define COLOR_MAX_COLORS       4


/*
 * Convert ZX Spectrum SCR to Atari color reference RGB.
 *
 * nearest == 0:
 *     fixed ZX -> GTIA mapping (-color)
 *
 * nearest != 0:
 *     nearest GTIA color mapping (-colorn)
 *
 * Result:
 *
 *     256x192 ZX
 *          ->
 *     160x240 logical Atari
 *          ->
 *     320x240 RGB
 *
 * The 192 source lines are placed at:
 *
 *     y = 24 ... 215
 *
 * The remaining 24 lines at the top and bottom are black.
 *
 * Returns:
 *
 *     1  conversion successful
 *     0  more than four final Atari colors
 *        or invalid input
 *
 * unique_colors receives the number of distinct final
 * Atari colors when conversion succeeds.
 */
int convert_scr_to_color_reference(
    const uint8_t *scr,
    uint8_t *rgb,
    int nearest,
    unsigned *unique_colors);

#endif