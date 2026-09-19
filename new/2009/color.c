#include "color.h"

#include "scr.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ========================================================= */
/* ZX -> Atari color reference image                          */
/*                                                           */
/* 256x192 ZX image                                          */
/*      -> 192x192 logical Atari pixels                      */
/*      -> 384x192 physical/reference image                  */
/*                                                           */
/* -color  : fixed 16-entry ZX -> GTIA mapping               */
/* -colorn : nearest GTIA color for each ZX palette entry    */
/*                                                           */
/* No dithering. No blending. No color mixing.               */
/* ========================================================= */

#define ATARI_LOGICAL_WIDTH   192
#define ATARI_PHYSICAL_WIDTH  384
#define ATARI_COLOR_COUNT     256


/*
 * Fixed ZX -> GTIA mapping.
 *
 * This table was calculated once from the ZX RGB888 palette
 * against the current 256-entry Atari GTIA RGB888 palette.
 *
 * Index 0..15 is the ZX Spectrum color index.
 */
static const uint8_t zx_to_gtia_fixed[16] =
{
    0,    /*  0: black       -> 0x00 */
    112,  /*  1: blue        -> 0x70 */
    66,   /*  2: red         -> 0x42 */
    103,  /*  3: magenta     -> 0x67 */
    197,  /*  4: green       -> 0xc5 */
    169,  /*  5: cyan        -> 0xa9 */
    27,   /*  6: yellow      -> 0x1b */
    12,   /*  7: white       -> 0x0c */

    0,    /*  8: bright black -> 0x00 */
    113,  /*  9: bright blue  -> 0x71 */
    67,   /* 10: bright red   -> 0x43 */
    105,  /* 11: bright magenta -> 0x69 */
    198,  /* 12: bright green -> 0xc6 */
    172,  /* 13: bright cyan  -> 0xac */
    30,   /* 14: bright yellow -> 0x1e */
    15    /* 15: bright white -> 0x0f */
};

/* ========================================================= */
/* Atari GTIA palette                                        */
/*                                                           */
/* 256 RGB888 entries.                                       */
/* Index == Atari GTIA color value.                          */
/* Source: atari-8-bit-series-gtia.hex                       */
/* ========================================================= */

static const uint8_t atari_gtia_palette[256][3] =
{
    { 0x00, 0x00, 0x00 },
    { 0x11, 0x11, 0x11 },
    { 0x22, 0x22, 0x22 },
    { 0x33, 0x33, 0x33 },
    { 0x44, 0x44, 0x44 },
    { 0x55, 0x55, 0x55 },
    { 0x66, 0x66, 0x66 },
    { 0x77, 0x77, 0x77 },
    { 0x88, 0x88, 0x88 },
    { 0x99, 0x99, 0x99 },
    { 0xaa, 0xaa, 0xaa },
    { 0xbb, 0xbb, 0xbb },
    { 0xcc, 0xcc, 0xcc },
    { 0xdd, 0xdd, 0xdd },
    { 0xee, 0xee, 0xee },
    { 0xff, 0xff, 0xff },

    { 0x09, 0x19, 0x00 },
    { 0x19, 0x28, 0x06 },
    { 0x29, 0x37, 0x0d },
    { 0x3a, 0x47, 0x14 },
    { 0x4a, 0x56, 0x1b },
    { 0x5a, 0x65, 0x22 },
    { 0x6b, 0x75, 0x29 },
    { 0x7b, 0x84, 0x30 },
    { 0x8c, 0x93, 0x36 },
    { 0x9c, 0xa3, 0x3d },
    { 0xac, 0xb2, 0x44 },
    { 0xbd, 0xc1, 0x4b },
    { 0xcd, 0xd1, 0x52 },
    { 0xde, 0xe0, 0x59 },
    { 0xee, 0xef, 0x60 },
    { 0xff, 0xff, 0x67 },

    { 0x30, 0x00, 0x00 },
    { 0x3d, 0x11, 0x08 },
    { 0x4b, 0x22, 0x11 },
    { 0x59, 0x33, 0x19 },
    { 0x67, 0x44, 0x22 },
    { 0x75, 0x55, 0x2a },
    { 0x82, 0x66, 0x33 },
    { 0x90, 0x77, 0x3b },
    { 0x9e, 0x88, 0x44 },
    { 0xac, 0x99, 0x4c },
    { 0xba, 0xaa, 0x55 },
    { 0xc7, 0xbb, 0x5d },
    { 0xd5, 0xcc, 0x66 },
    { 0xe3, 0xdd, 0x6e },
    { 0xf1, 0xee, 0x77 },
    { 0xff, 0xff, 0x80 },

    { 0x4b, 0x00, 0x00 },
    { 0x57, 0x0f, 0x0c },
    { 0x63, 0x1e, 0x18 },
    { 0x6f, 0x2e, 0x24 },
    { 0x7a, 0x3d, 0x30 },
    { 0x87, 0x4d, 0x3c },
    { 0x93, 0x5c, 0x49 },
    { 0x9f, 0x6b, 0x55 },
    { 0xab, 0x7b, 0x61 },
    { 0xb6, 0x8a, 0x6d },
    { 0xc3, 0x9a, 0x79 },
    { 0xcf, 0xa9, 0x86 },
    { 0xdb, 0xb8, 0x92 },
    { 0xe6, 0xc8, 0x9e },
    { 0xf3, 0xd7, 0xaa },
    { 0xff, 0xe7, 0xb7 },

    { 0x55, 0x00, 0x00 },
    { 0x60, 0x0e, 0x10 },
    { 0x6b, 0x1c, 0x21 },
    { 0x77, 0x2a, 0x32 },
    { 0x82, 0x38, 0x43 },
    { 0x8d, 0x46, 0x54 },
    { 0x99, 0x54, 0x65 },
    { 0xa4, 0x62, 0x76 },
    { 0xaf, 0x71, 0x87 },
    { 0xbb, 0x7f, 0x98 },
    { 0xc6, 0x8d, 0xa9 },
    { 0xd1, 0x9b, 0xba },
    { 0xdd, 0xa9, 0xcb },
    { 0xe8, 0xb7, 0xdc },
    { 0xf3, 0xc5, 0xed },
    { 0xff, 0xd4, 0xfe },

    { 0x4c, 0x00, 0x47 },
    { 0x57, 0x0d, 0x53 },
    { 0x63, 0x1b, 0x5f },
    { 0x6f, 0x28, 0x6b },
    { 0x7b, 0x36, 0x78 },
    { 0x87, 0x43, 0x84 },
    { 0x93, 0x51, 0x90 },
    { 0x9f, 0x5e, 0x9c },
    { 0xab, 0x6c, 0xa9 },
    { 0xb7, 0x79, 0xb5 },
    { 0xc3, 0x87, 0xc1 },
    { 0xcf, 0x94, 0xcd },
    { 0xdb, 0xa2, 0xda },
    { 0xe7, 0xaf, 0xe6 },
    { 0xf3, 0xbd, 0xf2 },
    { 0xff, 0xcb, 0xff },

    { 0x30, 0x00, 0x7e },
    { 0x3b, 0x0b, 0x85 },
    { 0x49, 0x19, 0x8d },
    { 0x57, 0x27, 0x96 },
    { 0x65, 0x34, 0x9f },
    { 0x72, 0x42, 0xa7 },
    { 0x80, 0x50, 0xb0 },
    { 0x8e, 0x5d, 0xb8 },
    { 0x9c, 0x6b, 0xc1 },
    { 0xa9, 0x79, 0xc9 },
    { 0xb7, 0x86, 0xd2 },
    { 0xc5, 0x94, 0xdb },
    { 0xd3, 0xa2, 0xe3 },
    { 0xe0, 0xaf, 0xec },
    { 0xee, 0xbd, 0xf4 },
    { 0xfc, 0xcb, 0xfd },

    { 0x0a, 0x00, 0x97 },
    { 0x1a, 0x0e, 0x9d },
    { 0x2a, 0x1d, 0xa4 },
    { 0x3b, 0x2c, 0xab },
    { 0x4b, 0x3a, 0xb2 },
    { 0x5b, 0x49, 0xb9 },
    { 0x6c, 0x58, 0xc0 },
    { 0x7c, 0x67, 0xc7 },
    { 0x8c, 0x75, 0xce },
    { 0x9c, 0x84, 0xd5 },
    { 0xad, 0x93, 0xdc },
    { 0xbd, 0xa2, 0xe3 },
    { 0xce, 0xb0, 0xea },
    { 0xde, 0xbf, 0xf1 },
    { 0xee, 0xce, 0xf8 },
    { 0xff, 0xdd, 0xff },

    { 0x00, 0x00, 0x8e },
    { 0x0c, 0x0d, 0x94 },
    { 0x1b, 0x1e, 0x9c },
    { 0x2a, 0x2e, 0xa3 },
    { 0x39, 0x3e, 0xab },
    { 0x48, 0x4e, 0xb2 },
    { 0x57, 0x5e, 0xba },
    { 0x66, 0x6e, 0xc1 },
    { 0x74, 0x7e, 0xc9 },
    { 0x83, 0x8f, 0xd0 },
    { 0x92, 0x9f, 0xd8 },
    { 0xa1, 0xaf, 0xdf },
    { 0xb0, 0xbf, 0xe6 },
    { 0xbf, 0xcf, 0xee },
    { 0xce, 0xdf, 0xf5 },
    { 0xdd, 0xef, 0xfd },

    { 0x00, 0x0e, 0x64 },
    { 0x0c, 0x1e, 0x6e },
    { 0x19, 0x2e, 0x78 },
    { 0x26, 0x3e, 0x83 },
    { 0x32, 0x4e, 0x8d },
    { 0x3f, 0x5e, 0x97 },
    { 0x4c, 0x6e, 0xa2 },
    { 0x58, 0x7e, 0xac },
    { 0x65, 0x8e, 0xb6 },
    { 0x72, 0x9e, 0xc1 },
    { 0x7e, 0xae, 0xcb },
    { 0x8b, 0xbe, 0xd5 },
    { 0x98, 0xce, 0xe0 },
    { 0xa4, 0xde, 0xea },
    { 0xb1, 0xee, 0xf4 },
    { 0xbe, 0xff, 0xff },

    { 0x00, 0x24, 0x22 },
    { 0x09, 0x30, 0x2e },
    { 0x15, 0x3f, 0x3d },
    { 0x20, 0x4d, 0x4c },
    { 0x2c, 0x5c, 0x5a },
    { 0x37, 0x6a, 0x69 },
    { 0x42, 0x79, 0x78 },
    { 0x4e, 0x87, 0x86 },
    { 0x59, 0x96, 0x95 },
    { 0x65, 0xa4, 0xa4 },
    { 0x70, 0xb3, 0xb2 },
    { 0x7c, 0xc1, 0xc1 },
    { 0x87, 0xd0, 0xd0 },
    { 0x92, 0xdf, 0xde },
    { 0x9e, 0xed, 0xed },
    { 0xa9, 0xfc, 0xfc },

    { 0x00, 0x32, 0x00 },
    { 0x0b, 0x3f, 0x0e },
    { 0x16, 0x4d, 0x1c },
    { 0x22, 0x5b, 0x2b },
    { 0x2d, 0x68, 0x39 },
    { 0x39, 0x76, 0x48 },
    { 0x44, 0x84, 0x56 },
    { 0x50, 0x91, 0x64 },
    { 0x5b, 0x9f, 0x73 },
    { 0x67, 0xad, 0x81 },
    { 0x72, 0xba, 0x90 },
    { 0x7e, 0xc8, 0x9e },
    { 0x89, 0xd6, 0xac },
    { 0x95, 0xe3, 0xbb },
    { 0xa0, 0xf1, 0xc9 },
    { 0xac, 0xff, 0xd8 },

    { 0x00, 0x34, 0x00 },
    { 0x0c, 0x41, 0x0a },
    { 0x19, 0x4f, 0x14 },
    { 0x26, 0x5c, 0x1e },
    { 0x33, 0x6a, 0x28 },
    { 0x40, 0x77, 0x32 },
    { 0x4c, 0x85, 0x3c },
    { 0x59, 0x92, 0x46 },
    { 0x66, 0xa0, 0x50 },
    { 0x73, 0xad, 0x5a },
    { 0x80, 0xbb, 0x64 },
    { 0x8c, 0xc8, 0x6e },
    { 0x99, 0xd6, 0x78 },
    { 0xa6, 0xe3, 0x82 },
    { 0xb3, 0xf1, 0x8c },
    { 0xc0, 0xff, 0x97 },

    { 0x00, 0x2a, 0x00 },
    { 0x0f, 0x38, 0x07 },
    { 0x1e, 0x46, 0x0e },
    { 0x2d, 0x54, 0x16 },
    { 0x3c, 0x62, 0x1d },
    { 0x4b, 0x71, 0x24 },
    { 0x5a, 0x7f, 0x2c },
    { 0x69, 0x8d, 0x33 },
    { 0x79, 0x9b, 0x3b },
    { 0x88, 0xa9, 0x42 },
    { 0x97, 0xb8, 0x49 },
    { 0xa6, 0xc6, 0x51 },
    { 0xb5, 0xd4, 0x58 },
    { 0xc4, 0xe2, 0x60 },
    { 0xd3, 0xf0, 0x67 },
    { 0xe3, 0xff, 0x6f },

    { 0x0d, 0x17, 0x00 },
    { 0x1d, 0x26, 0x06 },
    { 0x2d, 0x35, 0x0d },
    { 0x3d, 0x45, 0x14 },
    { 0x4d, 0x54, 0x1b },
    { 0x5d, 0x64, 0x22 },
    { 0x6d, 0x73, 0x29 },
    { 0x7d, 0x83, 0x30 },
    { 0x8e, 0x92, 0x37 },
    { 0x9e, 0xa2, 0x3e },
    { 0xae, 0xb1, 0x45 },
    { 0xbe, 0xc1, 0x4c },
    { 0xce, 0xd0, 0x53 },
    { 0xde, 0xe0, 0x5a },
    { 0xee, 0xef, 0x61 },
    { 0xff, 0xff, 0x68 },

    { 0x33, 0x00, 0x00 },
    { 0x40, 0x10, 0x08 },
    { 0x4e, 0x21, 0x11 },
    { 0x5b, 0x32, 0x1a },
    { 0x69, 0x43, 0x23 },
    { 0x77, 0x54, 0x2c },
    { 0x84, 0x65, 0x35 },
    { 0x92, 0x76, 0x3e },
    { 0x9f, 0x86, 0x46 },
    { 0xad, 0x97, 0x4f },
    { 0xbb, 0xa8, 0x58 },
    { 0xc8, 0xb9, 0x61 },
    { 0xd6, 0xca, 0x6a },
    { 0xe3, 0xdb, 0x73 },
    { 0xf1, 0xec, 0x7c },
    { 0xff, 0xfd, 0x85 }
};

/*
 * ZX Spectrum palette.
 *
 * These are the exact RGB values used by scr.c.
 *
 * Index 0..7  = normal
 * Index 8..15 = bright
 */
static const uint8_t zx_palette[16][3] =
{
    {   0,   0,   0 }, /* 0  black   */
    {   0,   0, 205 }, /* 1  blue    */
    { 205,   0,   0 }, /* 2  red     */
    { 205,   0, 205 }, /* 3  magenta */
    {   0, 205,   0 }, /* 4  green   */
    {   0, 205, 205 }, /* 5  cyan    */
    { 205, 205,   0 }, /* 6  yellow  */
    { 205, 205, 205 }, /* 7  white   */

    {   0,   0,   0 }, /* 8  black   */
    {   0,   0, 255 }, /* 9  blue    */
    { 255,   0,   0 }, /* 10 red     */
    { 255,   0, 255 }, /* 11 magenta */
    {   0, 255,   0 }, /* 12 green   */
    {   0, 255, 255 }, /* 13 cyan    */
    { 255, 255,   0 }, /* 14 yellow  */
    { 255, 255, 255 }  /* 15 white   */
};


/*
 * Fixed ZX -> GTIA mapping used by -color.
 *
 * These are GTIA palette indexes.
 *
 * Normal:
 *
 *   black   ->   0
 *   blue    -> 112
 *   red     ->  66
 *   magenta -> 103
 *   green   -> 197
 *   cyan    -> 169
 *   yellow  ->  27
 *   white   ->  12
 *
 * Bright:
 *
 *   black   ->   0
 *   blue    -> 113
 *   red     ->  67
 *   magenta -> 105
 *   green   -> 198
 *   cyan    -> 172
 *   yellow  ->  30
 *   white   ->  15
 */
static const uint8_t fixed_zx_to_gtia[16] =
{
      0, 112,  66, 103,
    197, 169,  27,  12,

      0, 113,  67, 105,
    198, 172,  30,  15
};



/*
 * Return the ZX palette index for a pixel.
 *
 * ZX bitmap:
 *
 *     6144 bytes
 *
 * ZX attributes:
 *
 *     768 bytes
 *
 * The ZX screen bitmap uses the well-known
 * interleaved Spectrum layout.
 */
static unsigned zx_pixel_index(const uint8_t *scr,
                                unsigned x,
                                unsigned y)
{
    unsigned bitmap_offset;
    uint8_t bitmap_byte;
    uint8_t attr;

    unsigned ink;
    unsigned paper;

    /*
     * ZX bitmap address:
     *
     * 010 TTSSS LLL XXXXX
     */
    bitmap_offset =
        ((y & 0xC0) << 5) |
        ((y & 0x07) << 8) |
        ((y & 0x38) << 2) |
        (x >> 3);

    bitmap_byte =
        scr[bitmap_offset];

    /*
     * Bit 7 is the leftmost pixel.
     */
    if (bitmap_byte & (0x80u >> (x & 7)))
        bitmap_offset = 1;
    else
        bitmap_offset = 0;

    /*
     * Attribute:
     *
     *     0RRR FBBB
     */
    attr =
        scr[BITMAP_SIZE +
            (y >> 3) * 32 +
            (x >> 3)];

    paper = (attr >> 3) & 7;
    ink   = attr & 7;

    /*
     * Bright applies to both ink and paper.
     */
    if (attr & 0x40)
    {
        paper += 8;
        ink += 8;
    }

    return bitmap_offset ? ink : paper;
}


/*
 * Find nearest GTIA color for one ZX RGB color.
 *
 * Squared Euclidean RGB distance is intentionally used
 * because the fixed mapping was generated using the same
 * metric.
 */
static unsigned nearest_gtia_color(unsigned zx_index)
{
    unsigned best;
    unsigned best_distance;
    unsigned i;

    const uint8_t *z;

    z = zx_palette[zx_index];

    best = 0;
    best_distance = 0xffffffffu;

    for (i = 0; i < 256; ++i)
    {
        int dr;
        int dg;
        int db;
        unsigned distance;

        dr = (int)z[0] - (int)atari_gtia_palette[i][0];
        dg = (int)z[1] - (int)atari_gtia_palette[i][1];
        db = (int)z[2] - (int)atari_gtia_palette[i][2];

        distance =
            (unsigned)(dr * dr +
                       dg * dg +
                       db * db);

        if (distance < best_distance)
        {
            best_distance = distance;
            best = i;
        }
    }

    return best;
}


/*
 * Build the 16-entry conversion table once.
 *
 * For -color:
 *
 *     fixed_zx_to_gtia[]
 *
 * For -colorn:
 *
 *     nearest GTIA matching.
 */
static void build_mapping(int nearest,
                          uint8_t mapping[16])
{
    unsigned i;

    if (!nearest)
    {
        memcpy(mapping,
               fixed_zx_to_gtia,
               sizeof(fixed_zx_to_gtia));

        return;
    }

    for (i = 0; i < 16; ++i)
        mapping[i] =
            (uint8_t)nearest_gtia_color(i);
}


/*
 * Return the number of different colors already stored.
 *
 * Since COLOR_MAX_COLORS is only four, a linear search is
 * faster and simpler than a palette/hash structure.
 */
static int add_color(unsigned color,
                     unsigned colors[COLOR_MAX_COLORS],
                     unsigned *count)
{
    unsigned i;

    for (i = 0; i < *count; ++i)
    {
        if (colors[i] == color)
            return 1;
    }

    if (*count >= COLOR_MAX_COLORS)
        return 0;

    colors[*count] = color;
    ++*count;

    return 1;
}


/*
 * Convert one Atari GTIA palette entry to RGB.
 */
static void gtia_to_rgb(unsigned color,
                         uint8_t *r,
                         uint8_t *g,
                         uint8_t *b)
{
    *r = atari_gtia_palette[color][0];
    *g = atari_gtia_palette[color][1];
    *b = atari_gtia_palette[color][2];
}


/*
 * Main conversion.
 *
 * Horizontal:
 *
 *     256 ZX pixels
 *          ->
 *     160 Atari logical pixels
 *
 * Vertical:
 *
 *     192 ZX lines
 *          ->
 *     rows 24..215
 *
 * Output:
 *
 *     320x240 RGB
 */
int convert_scr_to_color_reference(
    const uint8_t *scr,
    uint8_t *rgb,
    int nearest,
    unsigned *unique_colors)
{
    uint8_t mapping[16];

    unsigned colors[COLOR_MAX_COLORS];
    unsigned color_count;

    unsigned x;
    unsigned y;

    if (!scr || !rgb)
        return 0;

    build_mapping(nearest, mapping);

    /*
     * Start with a completely black 320x240 image.
     *
     * This creates the 24-pixel top and bottom borders.
     */
    memset(
        rgb,
        0,
        (size_t)COLOR_PHYSICAL_WIDTH *
        COLOR_PHYSICAL_HEIGHT *
        3);

    color_count = 0;

    /*
     * We need to know the final palette before accepting
     * the image.
     *
     * At the same time we generate the output.
     *
     * Horizontal mapping:
     *
     *     source x = x * 256 / 160
     *
     * This is nearest/center sampling without interpolation.
     */
    for (y = 0; y < COLOR_SOURCE_HEIGHT; ++y)
    {
        unsigned out_y;

        out_y =
            y + COLOR_TOP_BORDER;

        for (x = 0; x < COLOR_LOGICAL_WIDTH; ++x)
        {
            unsigned source_x;
            unsigned zx_index;
            unsigned gtia;

            uint8_t r;
            uint8_t g;
            uint8_t b;

            size_t out_offset;


            /*
             * Center-sampled 256 -> 160 conversion.
             *
             * x = 0     -> source near 0
             * x = 159   -> source near 255
             */
            source_x =
                (x * COLOR_SOURCE_WIDTH +
                 COLOR_LOGICAL_WIDTH / 2) /
                COLOR_LOGICAL_WIDTH;

            if (source_x >= COLOR_SOURCE_WIDTH)
                source_x =
                    COLOR_SOURCE_WIDTH - 1;


            zx_index =
                zx_pixel_index(scr,
                               source_x,
                               y);

            gtia =
                mapping[zx_index];


            /*
             * Count final GTIA colors, not ZX colors.
             *
             * Therefore two different ZX colors which map
             * to the same Atari color consume only one slot.
             */
            if (!add_color(gtia,
                           colors,
                           &color_count))
            {
                if (unique_colors)
                    *unique_colors = COLOR_MAX_COLORS + 1;

                return 0;
            }


            gtia_to_rgb(gtia,
                        &r,
                        &g,
                        &b);


            /*
             * One logical Atari pixel becomes two RGB pixels.
             */
            out_offset =
                ((size_t)out_y *
                 COLOR_PHYSICAL_WIDTH +
                 (size_t)x * 2) * 3;


            rgb[out_offset + 0] = r;
            rgb[out_offset + 1] = g;
            rgb[out_offset + 2] = b;

            rgb[out_offset + 3] = r;
            rgb[out_offset + 4] = g;
            rgb[out_offset + 5] = b;
        }
    }

    if (unique_colors)
        *unique_colors = color_count;

    return 1;
}