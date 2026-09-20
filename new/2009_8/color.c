#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "color.h"
#include "scr.h"


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

static const uint8_t zx_palette[16][3] =
{
    {   0,   0,   0 },
    {   0,   0, 205 },
    { 205,   0,   0 },
    { 205,   0, 205 },
    {   0, 205,   0 },
    {   0, 205, 205 },
    { 205, 205,   0 },
    { 205, 205, 205 },

    {   0,   0,   0 },
    {   0,   0, 255 },
    { 255,   0,   0 },
    { 255,   0, 255 },
    {   0, 255,   0 },
    {   0, 255, 255 },
    { 255, 255,   0 },
    { 255, 255, 255 }
};


/*
 * Fixed ZX -> GTIA mapping used by -color.
 */
static const uint8_t fixed_zx_to_gtia[16] =
{
      0, 112,  66, 103,
    197, 169,  27,  12,

      0, 113,  67, 105,
    198, 172,  30,  15
};


/*
 * Return ZX palette index for one source pixel.
 */
static unsigned zx_pixel_index(
    const uint8_t *scr,
    unsigned x,
    unsigned y)
{
    unsigned bitmap_offset;
    uint8_t bitmap_byte;
    uint8_t attr;

    unsigned ink;
    unsigned paper;

    bitmap_offset =
        ((y & 0xC0) << 5) |
        ((y & 0x07) << 8) |
        ((y & 0x38) << 2) |
        (x >> 3);

    bitmap_byte = scr[bitmap_offset];

    attr =
        scr[BITMAP_SIZE +
            (y >> 3) * 32 +
            (x >> 3)];

    paper = (attr >> 3) & 7;
    ink   = attr & 7;

    if (attr & 0x40)
    {
        paper += 8;
        ink += 8;
    }

    return (bitmap_byte & (0x80u >> (x & 7)))
        ? ink
        : paper;
}


/*
 * Find nearest Atari GTIA RGB color for one ZX palette entry.
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
            (unsigned)(
                dr * dr +
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
 * Count ZX colors used by the image.
 *
 * pair_count[i]  = normal + bright pixels of ZX color i
 * bright_count[i] = bright pixels of ZX color i
 *
 * The two brightness levels of one ZX color form one logical pair.
 */
static void count_zx_colors(
    const uint8_t *scr,
    unsigned pair_count[8],
    unsigned bright_count[8])
{
    unsigned x;
    unsigned y;

    memset(pair_count, 0, 8 * sizeof(unsigned));
    memset(bright_count, 0, 8 * sizeof(unsigned));

    for (y = 0; y < COLOR_SOURCE_HEIGHT; ++y)
    {
        for (x = 0; x < COLOR_SOURCE_WIDTH; ++x)
        {
            unsigned zx_index;
            unsigned base;

            zx_index =
                zx_pixel_index(
                    scr,
                    x,
                    y);

            base = zx_index & 7;

            ++pair_count[base];

            if (zx_index & 8)
                ++bright_count[base];
        }
    }
}

/*
 * Luminance of one ZX palette entry.
 *
 * Normal and bright variants are intentionally treated
 * independently here. They are merged only when selecting
 * the four most popular logical components.
 */
static unsigned zx_color_luminance(unsigned zx_index)
{
    const uint8_t *p = zx_palette[zx_index & 15];

    return
        (299u * p[0] +
         587u * p[1] +
         114u * p[2] +
         500u) / 1000u;
}


/*
 * Select up to four most popular logical ZX color components.
 *
 * Component 0..7 includes both:
 *
 *     normal component
 *     bright component
 *
 * This is the same "bright merged" rule used by -colorn.
 */
static unsigned select_grey_components(
    const uint8_t *scr,
    unsigned components[COLOR_MAX_COLORS])
{
    unsigned pair_count[8];
    unsigned bright_count[8];
    unsigned selected[8];
    unsigned count;
    unsigned i;

    count_zx_colors(
        scr,
        pair_count,
        bright_count);

    (void)bright_count;

    for (i = 0; i < 8; ++i)
        selected[i] = 0;

    /*
     * First select the most popular logical ZX components.
     *
     * Normal + bright versions of the same ZX color
     * are treated as one component, exactly as in -colorn.
     */
    count = 0;

    while (count < COLOR_MAX_COLORS)
    {
        unsigned best;
        unsigned best_count;

        best = 8;
        best_count = 0;

        for (i = 0; i < 8; ++i)
        {
            if (selected[i])
                continue;

            if (pair_count[i] > best_count)
            {
                best = i;
                best_count = pair_count[i];
            }
        }

        if (best == 8 || best_count == 0)
            break;

        components[count++] = best;
        selected[best] = 1;
    }

    return count;
}

static unsigned build_grey_levels(
    const uint8_t *scr,
    const unsigned components[COLOR_MAX_COLORS],
    unsigned component_count,
    unsigned levels[COLOR_MAX_COLORS])
{
    unsigned present[8];
    unsigned i;
    unsigned count;

    for (i = 0; i < 8; ++i)
        present[i] = 0;

    /*
     * Determine which logical ZX components are actually present.
     */
    {
        unsigned pair_count[8];
        unsigned bright_count[8];

        count_zx_colors(
            scr,
            pair_count,
            bright_count);

        (void)bright_count;

        for (i = 0; i < 8; ++i)
        {
            if (pair_count[i] != 0)
                present[i] = 1;
        }
    }

    count = 0;

    /*
     * Always keep black if it exists.
     */
    if (present[0])
        levels[count++] = 0;

    /*
     * Add luminance of the selected popular components.
     *
     * Avoid duplicates.
     */
    for (i = 0; i < component_count; ++i)
    {
        unsigned level;
        unsigned j;
        int duplicate;

        level =
            zx_color_luminance(
                components[i]);

        duplicate = 0;

        for (j = 0; j < count; ++j)
        {
            if (levels[j] == level)
            {
                duplicate = 1;
                break;
            }
        }

        if (!duplicate && count < COLOR_MAX_COLORS)
            levels[count++] = level;
    }

    /*
     * Always keep white if it exists.
     *
     * ZX white is component 7.
     */
    if (present[7] && count < COLOR_MAX_COLORS)
    {
        int duplicate;

        duplicate = 0;

        for (i = 0; i < count; ++i)
        {
            if (levels[i] == 255)
            {
                duplicate = 1;
                break;
            }
        }

        if (!duplicate)
            levels[count++] = 255;
    }

    /*
     * Sort levels from dark to bright.
     */
    for (i = 0; i < count; ++i)
    {
        unsigned j;

        for (j = i + 1; j < count; ++j)
        {
            if (levels[j] < levels[i])
            {
                unsigned t;

                t = levels[i];
                levels[i] = levels[j];
                levels[j] = t;
            }
        }
    }

    /*
     * If BLACK + selected components + WHITE produced
     * more than four levels, keep the four most useful ones.
     *
     * In practice this can happen when both black and white
     * are present and all four popular components are distinct.
     *
     * Keep black and white, and select the two most popular
     * component luminances between them.
     */
    if (count > COLOR_MAX_COLORS)
    {
        unsigned new_levels[COLOR_MAX_COLORS];
        unsigned new_count;
        unsigned best_component[2];
        unsigned used[COLOR_MAX_COLORS];

        new_count = 0;

        /*
         * Black.
         */
        if (present[0])
            new_levels[new_count++] = 0;

        /*
         * Select the most popular non-black/non-white
         * components by pair_count.
         */
        {
            unsigned pair_count[8];
            unsigned bright_count[8];
            unsigned used_component[8];

            count_zx_colors(
                scr,
                pair_count,
                bright_count);

            (void)bright_count;

            for (i = 0; i < 8; ++i)
                used_component[i] = 0;

            used_component[0] = 1;
            used_component[7] = 1;

            best_component[0] = 8;
            best_component[1] = 8;

            for (i = 0; i < 2; ++i)
            {
                unsigned best;
                unsigned best_count;

                best = 8;
                best_count = 0;

                {
                    unsigned c;

                    for (c = 0; c < 8; ++c)
                    {
                        if (used_component[c])
                            continue;

                        if (pair_count[c] > best_count)
                        {
                            best = c;
                            best_count = pair_count[c];
                        }
                    }
                }

                if (best != 8 && best_count != 0)
                {
                    best_component[i] = best;
                    used_component[best] = 1;
                }
            }

            for (i = 0; i < 2; ++i)
            {
                if (best_component[i] != 8 &&
                    new_count < COLOR_MAX_COLORS - 1)
                {
                    new_levels[new_count++] =
                        zx_color_luminance(
                            best_component[i]);
                }
            }
        }

        /*
         * White.
         */
        if (present[7] &&
            new_count < COLOR_MAX_COLORS)
        {
            new_levels[new_count++] = 255;
        }

        /*
         * If there are fewer than four levels, fill from
         * the previously selected levels.
         */
        for (i = 0;
             i < count && new_count < COLOR_MAX_COLORS;
             ++i)
        {
            unsigned j;
            int duplicate;

            duplicate = 0;

            for (j = 0; j < new_count; ++j)
            {
                if (new_levels[j] == levels[i])
                {
                    duplicate = 1;
                    break;
                }
            }

            if (!duplicate)
                new_levels[new_count++] = levels[i];
        }

        for (i = 0; i < new_count; ++i)
            levels[i] = new_levels[i];

        count = new_count;

        /*
         * Sort again.
         */
        for (i = 0; i < count; ++i)
        {
            unsigned j;

            for (j = i + 1; j < count; ++j)
            {
                if (levels[j] < levels[i])
                {
                    unsigned t;

                    t = levels[i];
                    levels[i] = levels[j];
                    levels[j] = t;
                }
            }
        }

        (void)used;
    }

    return count;
}

/*
 * Find the nearest grayscale level.
 */
static unsigned nearest_grey_level(
    unsigned luminance,
    const unsigned levels[],
    unsigned level_count)
{
    unsigned best;
    unsigned best_distance;
    unsigned i;

    best = levels[0];

    if (luminance > best)
        best_distance = luminance - best;
    else
        best_distance = best - luminance;

    for (i = 1; i < level_count; ++i)
    {
        unsigned distance;

        if (luminance > levels[i])
            distance = luminance - levels[i];
        else
            distance = levels[i] - luminance;

        if (distance < best_distance)
        {
            best_distance = distance;
            best = levels[i];
        }
    }

    return best;
}

/*
 * Weighted squared error for one luminance interval.
 *
 * values[] contains distinct luminance values in ascending order.
 * weights[] contains their perceptual weights.
 *
 * The optimal grayscale value for the interval is its weighted mean.
 */
static uint64_t grey_interval_cost(
    const unsigned values[],
    const uint64_t prefix_w[],
    const uint64_t prefix_wy[],
    const uint64_t prefix_wy2[],
    unsigned first,
    unsigned last,
    unsigned *level)
{
    uint64_t weight;
    uint64_t sum_y;
    uint64_t sum_y2;
    uint64_t mean;
    uint64_t cost;

    weight =
        prefix_w[last + 1] -
        prefix_w[first];

    sum_y =
        prefix_wy[last + 1] -
        prefix_wy[first];

    sum_y2 =
        prefix_wy2[last + 1] -
        prefix_wy2[first];

    if (weight == 0)
    {
        *level = 0;
        return 0;
    }

    /*
     * Weighted mean, rounded to nearest integer.
     */
    mean =
        (sum_y + weight / 2) /
        weight;

    if (mean > 255)
        mean = 255;

    *level = (unsigned)mean;

    /*
     * SSE = sum(y^2) - 2*m*sum(y) + m^2*weight
     */
    cost =
        sum_y2 -
        2u * mean * sum_y +
        mean * mean * weight;

    return cost;
}


/*
 * Build four grayscale levels using perceptually weighted
 * one-dimensional quantization.
 *
 * The image is first converted to luminance.
 *
 * Pixels at strong local luminance transitions receive a
 * larger weight than pixels in flat regions.
 *
 * Therefore:
 *
 *     flat dark background     -> low importance
 *     object boundaries        -> high importance
 *     light/dark transitions   -> high importance
 *
 * The four final levels are found by exact dynamic programming
 * over the 0..255 luminance range.
 */
static unsigned build_smart_grey_levels(
    const uint8_t *scr,
    unsigned levels[COLOR_MAX_COLORS])
{
    /*
     * Weighted histogram of luminance.
     */
    uint64_t histogram[256];

    /*
     * Distinct luminance values actually present.
     */
    unsigned values[256];
    uint64_t weights[256];

    /*
     * Prefix sums:
     *
     * W
     * W*Y
     * W*Y^2
     */
    uint64_t prefix_w[257];
    uint64_t prefix_wy[257];
    uint64_t prefix_wy2[257];

    /*
     * DP:
     *
     * dp[k][i] =
     *     minimum weighted error for values 0..i
     *     represented by k grayscale levels.
     *
     * split[k][i] =
     *     first value of the final interval.
     */
    uint64_t dp[COLOR_MAX_COLORS + 1][256];
    unsigned split[COLOR_MAX_COLORS + 1][256];

    unsigned x;
    unsigned y;
    unsigned value_count;
    unsigned level_count;
    unsigned k;
    unsigned i;

    const uint64_t INF =
        UINT64_MAX / 4;

    if (!scr)
        return 0;

    /*
     * Clear histogram.
     */
    for (i = 0; i < 256; ++i)
        histogram[i] = 0;

    /*
     * Build perceptually weighted luminance histogram.
     *
     * Weight:
     *
     *     1 + local_contrast / 32
     *
     * limited to 8.
     *
     * Thus a completely flat pixel has weight 1,
     * while a strong edge can have weight up to 8.
     *
     * The intentionally modest range is important:
     * edges matter more, but cannot completely dominate
     * large image regions.
     */
    for (y = 0; y < COLOR_SOURCE_HEIGHT; ++y)
    {
        for (x = 0; x < COLOR_SOURCE_WIDTH; ++x)
        {
            unsigned center;
            unsigned left;
            unsigned right;
            unsigned up;
            unsigned down;

            unsigned contrast;
            unsigned weight;

            center =
                zx_color_luminance(
                    zx_pixel_index(
                        scr,
                        x,
                        y));

            if (x > 0)
            {
                left =
                    zx_color_luminance(
                        zx_pixel_index(
                            scr,
                            x - 1,
                            y));
            }
            else
            {
                left = center;
            }

            if (x + 1 < COLOR_SOURCE_WIDTH)
            {
                right =
                    zx_color_luminance(
                        zx_pixel_index(
                            scr,
                            x + 1,
                            y));
            }
            else
            {
                right = center;
            }

            if (y > 0)
            {
                up =
                    zx_color_luminance(
                        zx_pixel_index(
                            scr,
                            x,
                            y - 1));
            }
            else
            {
                up = center;
            }

            if (y + 1 < COLOR_SOURCE_HEIGHT)
            {
                down =
                    zx_color_luminance(
                        zx_pixel_index(
                            scr,
                            x,
                            y + 1));
            }
            else
            {
                down = center;
            }

            if (center > left)
                contrast = center - left;
            else
                contrast = left - center;

            if (center > right)
                contrast += center - right;
            else
                contrast += right - center;

            if (center > up)
                contrast += center - up;
            else
                contrast += up - center;

            if (center > down)
                contrast += center - down;
            else
                contrast += down - center;

            weight =
                1 + contrast / 32;

            if (weight > 8)
                weight = 8;

            histogram[center] += weight;
        }
    }

    /*
     * Convert histogram into a compact list of distinct
     * luminance values.
     */
    value_count = 0;

    for (i = 0; i < 256; ++i)
    {
        if (histogram[i] != 0)
        {
            values[value_count] = i;
            weights[value_count] = histogram[i];
            ++value_count;
        }
    }

    if (value_count == 0)
        return 0;

    /*
     * We cannot have more levels than distinct luminances.
     */
    level_count = value_count;

    if (level_count > COLOR_MAX_COLORS)
        level_count = COLOR_MAX_COLORS;

    /*
     * Prefix sums.
     */
    prefix_w[0] = 0;
    prefix_wy[0] = 0;
    prefix_wy2[0] = 0;

    for (i = 0; i < value_count; ++i)
    {
        uint64_t v;
        uint64_t w;

        v = values[i];
        w = weights[i];

        prefix_w[i + 1] =
            prefix_w[i] + w;

        prefix_wy[i + 1] =
            prefix_wy[i] + w * v;

        prefix_wy2[i + 1] =
            prefix_wy2[i] + w * v * v;
    }

    /*
     * Initialize DP.
     */
    for (k = 0;
         k <= COLOR_MAX_COLORS;
         ++k)
    {
        for (i = 0; i < 256; ++i)
        {
            dp[k][i] = INF;
            split[k][i] = 0;
        }
    }

    /*
     * One grayscale level.
     */
    for (i = 0; i < value_count; ++i)
    {
        unsigned level;

        dp[1][i] =
            grey_interval_cost(
                values,
                prefix_w,
                prefix_wy,
                prefix_wy2,
                0,
                i,
                &level);

        split[1][i] = 0;
    }

    /*
     * 2..4 grayscale levels.
     */
    for (k = 2;
         k <= level_count;
         ++k)
    {
        for (i = k - 1;
             i < value_count;
             ++i)
        {
            unsigned first;
            uint64_t best;

            best = INF;

            /*
             * Final interval is [first..i].
             *
             * There must be at least k-1 values before it,
             * therefore first >= k-1.
             */
            for (first = k - 1;
                 first <= i;
                 ++first)
            {
                uint64_t previous;
                uint64_t interval_cost;
                uint64_t total;
                unsigned interval_level;

                previous =
                    dp[k - 1][first - 1];

                if (previous == INF)
                    continue;

                interval_cost =
                    grey_interval_cost(
                        values,
                        prefix_w,
                        prefix_wy,
                        prefix_wy2,
                        first,
                        i,
                        &interval_level);

                total =
                    previous + interval_cost;

                if (total < best)
                {
                    best = total;
                    split[k][i] = first;
                }
            }

            dp[k][i] = best;
        }
    }

    /*
     * Reconstruct the optimal grayscale levels.
     *
     * The intervals are naturally ordered from dark to bright.
     */
    {
        unsigned end;

        end = value_count - 1;

        for (k = level_count;
             k > 0;
             --k)
        {
            unsigned first;
            unsigned interval_level;

            if (k == 1)
                first = 0;
            else
                first = split[k][end];

            (void)grey_interval_cost(
                values,
                prefix_w,
                prefix_wy,
                prefix_wy2,
                first,
                end,
                &interval_level);

            levels[k - 1] =
                interval_level;

            if (first == 0)
                break;

            end = first - 1;
        }
    }

    return level_count;
}

/*
 * Build a 256 -> 160 coordinate map using an integer
 * error accumulator.
 *
 * This is intentionally phase-zero Bresenham-style
 * sampling rather than the current nearest-neighbour
 * rounding formula.
 */
static void build_bres_x_map(
    unsigned map[COLOR_LOGICAL_WIDTH])
{
    unsigned source;
    unsigned error;
    unsigned x;

    source = 0;
    error = 0;

    for (x = 0; x < COLOR_LOGICAL_WIDTH; ++x)
    {
        map[x] = source;

        error += COLOR_SOURCE_WIDTH;

        while (error >= COLOR_LOGICAL_WIDTH)
        {
            error -= COLOR_LOGICAL_WIDTH;

            if (source + 1 < COLOR_SOURCE_WIDTH)
                ++source;
        }
    }
}


/*
 * Build a 192 -> 240 coordinate map using an integer
 * error accumulator.
 */
static void build_bres_y_map(
    unsigned map[COLOR_PHYSICAL_HEIGHT])
{
    unsigned source;
    unsigned error;
    unsigned y;

    source = 0;
    error = 0;

    for (y = 0; y < COLOR_PHYSICAL_HEIGHT; ++y)
    {
        map[y] = source;

        error += COLOR_SOURCE_HEIGHT;

        while (error >= COLOR_PHYSICAL_HEIGHT)
        {
            error -= COLOR_PHYSICAL_HEIGHT;

            if (source + 1 < COLOR_SOURCE_HEIGHT)
                ++source;
        }
    }
}

/*
 * Find the nearest allowed GTIA color for an RGB value.
 *
 * Unlike nearest_gtia_color(), this function searches only
 * the GTIA colors actually selected for the image.
 */
static unsigned nearest_allowed_gtia_color(
    unsigned r,
    unsigned g,
    unsigned b,
    const unsigned colors[],
    unsigned color_count)
{
    unsigned best;
    unsigned best_distance;
    unsigned i;

    best = colors[0];
    best_distance = 0xffffffffu;

    for (i = 0; i < color_count; ++i)
    {
        int dr;
        int dg;
        int db;
        unsigned distance;

        dr =
            (int)r -
            (int)atari_gtia_palette[colors[i]][0];

        dg =
            (int)g -
            (int)atari_gtia_palette[colors[i]][1];

        db =
            (int)b -
            (int)atari_gtia_palette[colors[i]][2];

        distance =
            (unsigned)(
                dr * dr +
                dg * dg +
                db * db);

        if (distance < best_distance)
        {
            best_distance = distance;
            best = colors[i];
        }
    }

    return best;
}

/*
 * Area/box resampling of one logical Atari pixel.
 *
 * Source:
 *
 *     256 x 192
 *
 * Destination:
 *
 *     160 x 240
 *
 * The source pixels are weighted by the actual area
 * covered by the destination pixel.
 *
 * The resulting RGB value is then quantized to one
 * of the allowed GTIA colors.
 */
static unsigned area_resample_pixel(
    const uint8_t *scr,
    unsigned x,
    unsigned y,
    const unsigned colors[],
    unsigned color_count)
{
    unsigned sx0;
    unsigned sx1;

    unsigned sy0;
    unsigned sy1;

    unsigned sx;
    unsigned sy;

    uint64_t sum_r;
    uint64_t sum_g;
    uint64_t sum_b;
    uint64_t total_weight;

    /*
     * Horizontal coordinates are represented with
     * denominator COLOR_LOGICAL_WIDTH.
     *
     * Destination pixel:
     *
     *     [x * 256 / 160,
     *      (x + 1) * 256 / 160]
     *
     * Source pixel:
     *
     *     [sx, sx + 1]
     */
    sx0 =
        (x * COLOR_SOURCE_WIDTH) /
        COLOR_LOGICAL_WIDTH;

    sx1 =
        ((x + 1) * COLOR_SOURCE_WIDTH - 1) /
        COLOR_LOGICAL_WIDTH;

    /*
     * Vertical coordinates use the same principle.
     */
    sy0 =
        (y * COLOR_SOURCE_HEIGHT) /
        COLOR_PHYSICAL_HEIGHT;

    sy1 =
        ((y + 1) * COLOR_SOURCE_HEIGHT - 1) /
        COLOR_PHYSICAL_HEIGHT;

    sum_r = 0;
    sum_g = 0;
    sum_b = 0;
    total_weight = 0;

    for (sy = sy0; sy <= sy1; ++sy)
    {
        unsigned y_start;
        unsigned y_end;
        unsigned y_left;
        unsigned y_right;
        unsigned wy;

        /*
         * All vertical coordinates are scaled by
         * COLOR_PHYSICAL_HEIGHT.
         */
        y_start =
            y * COLOR_SOURCE_HEIGHT;

        y_end =
            (y + 1) * COLOR_SOURCE_HEIGHT;

        y_left =
            sy * COLOR_PHYSICAL_HEIGHT;

        y_right =
            (sy + 1) * COLOR_PHYSICAL_HEIGHT;

        if (y_start > y_left)
            y_left = y_start;

        if (y_end < y_right)
            y_right = y_end;

        wy = y_right - y_left;

        if (wy == 0)
            continue;

        for (sx = sx0; sx <= sx1; ++sx)
        {
            unsigned x_start;
            unsigned x_end;
            unsigned x_left;
            unsigned x_right;
            unsigned wx;
            unsigned weight;

            unsigned zx_index;

            x_start =
                x * COLOR_SOURCE_WIDTH;

            x_end =
                (x + 1) * COLOR_SOURCE_WIDTH;

            x_left =
                sx * COLOR_LOGICAL_WIDTH;

            x_right =
                (sx + 1) * COLOR_LOGICAL_WIDTH;

            if (x_start > x_left)
                x_left = x_start;

            if (x_end < x_right)
                x_right = x_end;

            wx = x_right - x_left;

            if (wx == 0)
                continue;

            weight = wx * wy;

            zx_index =
                zx_pixel_index(
                    scr,
                    sx,
                    sy);

            sum_r +=
                (uint64_t)zx_palette[zx_index][0] *
                weight;

            sum_g +=
                (uint64_t)zx_palette[zx_index][1] *
                weight;

            sum_b +=
                (uint64_t)zx_palette[zx_index][2] *
                weight;

            total_weight += weight;
        }
    }

    if (total_weight == 0)
        return colors[0];

    /*
     * Round the RGB average instead of truncating it.
     */
    {
        unsigned r;
        unsigned g;
        unsigned b;

        r =
            (unsigned)(
                (sum_r + total_weight / 2) /
                total_weight);

        g =
            (unsigned)(
                (sum_g + total_weight / 2) /
                total_weight);

        b =
            (unsigned)(
                (sum_b + total_weight / 2) /
                total_weight);

        return nearest_allowed_gtia_color(
            r,
            g,
            b,
            colors,
            color_count);
    }
}

/*
 * Add a GTIA color to the set of used colors.
 *
 * Only four colors are allowed.
 */
static int add_color(
    unsigned color,
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
 * Build the final 16-entry ZX -> GTIA mapping.
 *
 * -color:
 *     fixed mapping, unchanged.
 *
 * -colorn:
 *
 *     1. Normal and bright variants initially share one
 *        GTIA color.
 *
 *     2. If fewer than four logical ZX color pairs are used,
 *        the free GTIA slots are used for the most frequently
 *        occurring bright variants.
 *
 * This allows an image such as:
 *
 *     blue + bright blue
 *     red  + bright red
 *     yellow + bright yellow
 *
 * to use:
 *
 *     blue
 *     red
 *     yellow
 *     bright red
 *
 * instead of throwing away the fourth available color.
 */
static void build_mapping(
    const uint8_t *scr,
    int nearest,
    uint8_t mapping[16])
{
    unsigned pair_count[8];
    unsigned bright_count[8];

    unsigned colors[COLOR_MAX_COLORS];
    unsigned color_count;

    unsigned i;

    if (!nearest)
    {
        memcpy(
            mapping,
            fixed_zx_to_gtia,
            sizeof(fixed_zx_to_gtia));

        return;
    }

    /*
     * Start with one GTIA color for each ZX logical color.
     *
     * Normal and bright are initially merged.
     */
    for (i = 0; i < 8; ++i)
    {
        uint8_t gtia;

        gtia =
            (uint8_t)nearest_gtia_color(i);

        mapping[i] = gtia;
        mapping[i + 8] = gtia;
    }

    count_zx_colors(
        scr,
        pair_count,
        bright_count);

    /*
     * Count the actual GTIA colors used by the initial
     * normal+bright merged mapping.
     */
    color_count = 0;

    for (i = 0; i < 8; ++i)
    {
        if (pair_count[i] == 0)
            continue;

        if (!add_color(
                mapping[i],
                colors,
                &color_count))
        {
            /*
             * This cannot normally happen because the maximum
             * number of colors is four, but keep the logic safe.
             */
            break;
        }
    }

    /*
     * Already have four colors: keep normal/bright merged.
     */
    if (color_count >= COLOR_MAX_COLORS)
        return;

    /*
     * Select bright variants in descending order of the number
     * of bright pixels.
     *
     * Only one bright variant can be selected for each logical
     * ZX color pair.
     */
    for (;;)
    {
        unsigned best;
        unsigned best_count;

        best = 8;
        best_count = 0;

        for (i = 0; i < 8; ++i)
        {
            unsigned bright_gtia;

            /*
             * No bright pixels -> no reason to split this pair.
             */
            if (bright_count[i] == 0)
                continue;

            /*
             * Already split.
             *
             * We use bright_count == 0xFFFFFFFF as an internal
             * "used" marker below.
             */
            if (bright_count[i] == 0xffffffffu)
                continue;

            if (bright_count[i] <= best_count)
                continue;

            bright_gtia =
                nearest_gtia_color(i + 8);

            /*
             * If the bright variant produces a GTIA color that
             * is already present, using a slot for it gives no
             * additional color.
             */
            {
                unsigned j;
                int already_used;

                already_used = 0;

                for (j = 0; j < color_count; ++j)
                {
                    if (colors[j] == bright_gtia)
                    {
                        already_used = 1;
                        break;
                    }
                }

                if (already_used)
                    continue;
            }

            best = i;
            best_count = bright_count[i];
        }

        /*
         * No useful bright candidate remains.
         */
        if (best == 8)
            break;

        /*
         * Introduce this bright variant as a separate GTIA color.
         */
        mapping[best + 8] =
            (uint8_t)nearest_gtia_color(best + 8);

        colors[color_count] =
            mapping[best + 8];

        ++color_count;

        /*
         * Mark this pair as already processed.
         */
        bright_count[best] = 0xffffffffu;

        if (color_count >= COLOR_MAX_COLORS)
            break;
    }
}


/*
 * Convert GTIA palette index to RGB.
 */
static void gtia_to_rgb(
    unsigned color,
    uint8_t *r,
    uint8_t *g,
    uint8_t *b)
{
    *r = atari_gtia_palette[color][0];
    *g = atari_gtia_palette[color][1];
    *b = atari_gtia_palette[color][2];
}


/*
 * 256x192 ZX
 *      |
 *      | nearest-neighbour
 *      v
 * 160x240 logical Atari
 *      |
 *      | horizontal pixel doubling
 *      v
 * 320x240 RGB
 */
int convert_scr_to_color_reference(
    const uint8_t *scr,
    uint8_t *rgb,
    int nearest,
    int resize_mode,
    unsigned *unique_colors)
{
    uint8_t mapping[16];

    unsigned colors[COLOR_MAX_COLORS];
    unsigned color_count;

    unsigned bres_x[COLOR_LOGICAL_WIDTH];
    unsigned bres_y[COLOR_PHYSICAL_HEIGHT];

    unsigned x;
    unsigned y;

    if (!scr || !rgb)
        return 0;

    /*
     * Build ZX -> GTIA mapping.
     *
     * The adaptive -colorn mapping is kept unchanged.
     */
    build_mapping(
        scr,
        nearest,
        mapping);

    /*
     * Bresenham coordinate maps are needed only
     * for -colorn-bres.
     */
    if (resize_mode == COLOR_RESIZE_BRES)
    {
        build_bres_x_map(bres_x);
        build_bres_y_map(bres_y);
    }

    /*
     * Determine the actual GTIA color set selected
     * by the mapping.
     *
     * Area mode is restricted to this same set.
     */
    color_count = 0;

    for (y = 0; y < COLOR_SOURCE_HEIGHT; ++y)
    {
        for (x = 0; x < COLOR_SOURCE_WIDTH; ++x)
        {
            unsigned zx_index;
            unsigned gtia;

            zx_index =
                zx_pixel_index(
                    scr,
                    x,
                    y);

            gtia =
                mapping[zx_index];

            if (!add_color(
                    gtia,
                    colors,
                    &color_count))
            {
                /*
                 * This is expected for images that contain
                 * more than four final GTIA colors.
                 */
                if (unique_colors)
                    *unique_colors =
                        COLOR_MAX_COLORS + 1;

                return 0;
            }
        }
    }

    /*
     * Area mode must have at least one allowed color.
     */
    if (color_count == 0)
        return 0;

    /*
     * Convert every logical Atari pixel.
     */
    for (y = 0; y < COLOR_PHYSICAL_HEIGHT; ++y)
    {
        for (x = 0; x < COLOR_LOGICAL_WIDTH; ++x)
        {
            unsigned gtia;

            uint8_t r;
            uint8_t g;
            uint8_t b;

            size_t out_offset;

            if (resize_mode == COLOR_RESIZE_AREA)
            {
                /*
                 * True area/box resampling.
                 */
                gtia =
                    area_resample_pixel(
                        scr,
                        x,
                        y,
                        colors,
                        color_count);
            }
            else
            {
                unsigned source_x;
                unsigned source_y;
                unsigned zx_index;

                if (resize_mode == COLOR_RESIZE_BRES)
                {
                    /*
                     * Integer error-accumulation resize.
                     */
                    source_x = bres_x[x];
                    source_y = bres_y[y];
                }
                else
                {
                    /*
                     * Existing nearest-neighbour behavior.
                     *
                     * Keep this exactly as it was.
                     */
                    source_y =
                        (y * COLOR_SOURCE_HEIGHT +
                         COLOR_PHYSICAL_HEIGHT / 2) /
                        COLOR_PHYSICAL_HEIGHT;

                    if (source_y >= COLOR_SOURCE_HEIGHT)
                        source_y =
                            COLOR_SOURCE_HEIGHT - 1;

                    source_x =
                        (x * COLOR_SOURCE_WIDTH +
                         COLOR_LOGICAL_WIDTH / 2) /
                        COLOR_LOGICAL_WIDTH;

                    if (source_x >= COLOR_SOURCE_WIDTH)
                        source_x =
                            COLOR_SOURCE_WIDTH - 1;
                }

                zx_index =
                    zx_pixel_index(
                        scr,
                        source_x,
                        source_y);

                gtia =
                    mapping[zx_index];
            }

            gtia_to_rgb(
                gtia,
                &r,
                &g,
                &b);

            /*
             * Logical Atari pixel becomes two physical
             * horizontal RGB pixels.
             */
            out_offset =
                ((size_t)y *
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

/*
 * Convert ZX Spectrum SCR to a four-level grayscale reference.
 *
 * Algorithm:
 *
 *     256x192 ZX
 *          |
 *          | four most popular logical color components
 *          | normal + bright merged for statistics
 *          v
 *     four luminance levels
 *          |
 *          | luminance quantization
 *          v
 *     160x240 logical Atari
 *          |
 *          | horizontal pixel doubling
 *          v
 *     320x240 RGB
 *
 * Important:
 *
 * The four colors are selected according to the frequency of
 * logical ZX color components, not according to RGB distance.
 *
 * Bright is merged with normal for component statistics.
 * Therefore a component such as blue consists of:
 *
 *     blue + bright blue
 *
 * but the actual pixels are still converted according to
 * their individual luminance before quantization.
 *
 * The result contains no more than four grayscale RGB values.
 */
int convert_scr_to_color_grey_reference(
    const uint8_t *scr,
    uint8_t *rgb,
    unsigned *unique_colors)
{
    unsigned components[COLOR_MAX_COLORS];
    unsigned levels[COLOR_MAX_COLORS];

    unsigned component_count;

    unsigned x;
    unsigned y;

    if (!scr || !rgb)
        return 0;

    /*
     * Find the four most frequently used logical ZX
     * color components.
     */
    component_count =
        select_grey_components(
            scr,
            components);

    if (component_count == 0)
        return 0;

    /*
     * Convert the selected components to their luminance.
     *
     * Bright is deliberately NOT a separate component here.
     * The component's normal palette color defines its
     * grayscale level.
     */
    for (x = 0; x < component_count; ++x)
    {
        levels[x] =
            zx_color_luminance(
                components[x]);
    }

    /*
     * Convert every destination pixel.
     *
     * Keep the existing 256 -> 160 nearest-neighbour geometry
     * used by the original -color/-colorn mode.
     */
    for (y = 0; y < COLOR_PHYSICAL_HEIGHT; ++y)
    {
        unsigned source_y;

        source_y =
            (y * COLOR_SOURCE_HEIGHT +
             COLOR_PHYSICAL_HEIGHT / 2) /
            COLOR_PHYSICAL_HEIGHT;

        if (source_y >= COLOR_SOURCE_HEIGHT)
            source_y =
                COLOR_SOURCE_HEIGHT - 1;

        for (x = 0; x < COLOR_LOGICAL_WIDTH; ++x)
        {
            unsigned source_x;
            unsigned zx_index;
            unsigned luminance;
            unsigned grey;

            size_t out_offset;

            source_x =
                (x * COLOR_SOURCE_WIDTH +
                 COLOR_LOGICAL_WIDTH / 2) /
                COLOR_LOGICAL_WIDTH;

            if (source_x >= COLOR_SOURCE_WIDTH)
                source_x =
                    COLOR_SOURCE_WIDTH - 1;

            /*
             * Get the original ZX pixel, including its
             * actual normal/bright state.
             */
            zx_index =
                zx_pixel_index(
                    scr,
                    source_x,
                    source_y);

            luminance =
                zx_color_luminance(
                    zx_index);

            /*
             * Quantize the actual pixel luminance to the
             * four selected grayscale levels.
             */
            grey =
                nearest_grey_level(
                    luminance,
                    levels,
                    component_count);

            out_offset =
                ((size_t)y *
                 COLOR_PHYSICAL_WIDTH +
                 (size_t)x * 2) * 3;

            /*
             * RGB grayscale.
             */
            rgb[out_offset + 0] =
                (uint8_t)grey;

            rgb[out_offset + 1] =
                (uint8_t)grey;

            rgb[out_offset + 2] =
                (uint8_t)grey;

            rgb[out_offset + 3] =
                (uint8_t)grey;

            rgb[out_offset + 4] =
                (uint8_t)grey;

            rgb[out_offset + 5] =
                (uint8_t)grey;
        }
    }

    if (unique_colors)
        *unique_colors = component_count;

    return 1;
}
/*
 * Four-level perceptually weighted grayscale reference.
 *
 * Unlike -colorn-grey, this mode does not select grayscale
 * levels from the frequency of ZX colors.
 *
 * Instead:
 *
 *     1. convert the source to luminance
 *     2. measure local luminance contrast
 *     3. give high-contrast pixels a larger weight
 *     4. find four optimal grayscale levels
 *     5. quantize the image to those levels
 *
 * This is intended to preserve visually important structures
 * even when they occupy only a small part of the image.
 */
int convert_scr_to_color_grey_smart_reference(
    const uint8_t *scr,
    uint8_t *rgb,
    unsigned *unique_colors)
{
    unsigned levels[COLOR_MAX_COLORS];
    unsigned level_count;

    unsigned x;
    unsigned y;

    if (!scr || !rgb)
        return 0;

    level_count =
        build_smart_grey_levels(
            scr,
            levels);

    if (level_count == 0)
        return 0;

    /*
     * Use the same geometry as the existing -colorn-grey:
     *
     *     256 -> 160
     *     192 -> 240
     *     horizontal x2
     *
     * This keeps the comparison between grey and
     * grey-smart clean.
     */
    for (y = 0;
         y < COLOR_PHYSICAL_HEIGHT;
         ++y)
    {
        unsigned source_y;

        source_y =
            (y * COLOR_SOURCE_HEIGHT +
             COLOR_PHYSICAL_HEIGHT / 2) /
            COLOR_PHYSICAL_HEIGHT;

        if (source_y >= COLOR_SOURCE_HEIGHT)
            source_y =
                COLOR_SOURCE_HEIGHT - 1;

        for (x = 0;
             x < COLOR_LOGICAL_WIDTH;
             ++x)
        {
            unsigned source_x;
            unsigned zx_index;
            unsigned luminance;
            unsigned grey;

            size_t out_offset;

            source_x =
                (x * COLOR_SOURCE_WIDTH +
                 COLOR_LOGICAL_WIDTH / 2) /
                COLOR_LOGICAL_WIDTH;

            if (source_x >= COLOR_SOURCE_WIDTH)
                source_x =
                    COLOR_SOURCE_WIDTH - 1;

            zx_index =
                zx_pixel_index(
                    scr,
                    source_x,
                    source_y);

            luminance =
                zx_color_luminance(
                    zx_index);

            grey =
                nearest_grey_level(
                    luminance,
                    levels,
                    level_count);

            out_offset =
                ((size_t)y *
                 COLOR_PHYSICAL_WIDTH +
                 (size_t)x * 2) * 3;

            rgb[out_offset + 0] =
                (uint8_t)grey;

            rgb[out_offset + 1] =
                (uint8_t)grey;

            rgb[out_offset + 2] =
                (uint8_t)grey;

            rgb[out_offset + 3] =
                (uint8_t)grey;

            rgb[out_offset + 4] =
                (uint8_t)grey;

            rgb[out_offset + 5] =
                (uint8_t)grey;
        }
    }

    if (unique_colors)
        *unique_colors = level_count;

    return 1;
}
/*
 * Floyd-Steinberg dithering of a 160x240 luminance image
 * using the four grayscale levels selected by smart analysis.
 *
 * Important:
 *
 *     dithering is performed AFTER the 256x192 -> 160x240
 *     geometry conversion.
 *
 * This prevents the resize operation from destroying the
 * dithering pattern.
 *
 * Serpentine scanning is used to reduce directional artifacts.
 */
static void dither_grey_levels(
    uint8_t *image,
    const unsigned levels[COLOR_MAX_COLORS],
    unsigned level_count)
{
    int error[COLOR_LOGICAL_WIDTH + 2];
    int next_error[COLOR_LOGICAL_WIDTH + 2];

    unsigned y;

    if (!image || !levels || level_count == 0)
        return;

    memset(
        error,
        0,
        sizeof(error));

    memset(
        next_error,
        0,
        sizeof(next_error));

    for (y = 0;
         y < COLOR_LOGICAL_HEIGHT;
         ++y)
    {
        unsigned x;
        int left_to_right;

        /*
         * Serpentine scan.
         */
        left_to_right =
            ((y & 1u) == 0);

        memset(
            next_error,
            0,
            sizeof(next_error));

        if (left_to_right)
        {
            for (x = 0;
                 x < COLOR_LOGICAL_WIDTH;
                 ++x)
            {
                size_t offset;
                int value;
                unsigned grey;
                int diff;

                offset =
                    (size_t)y *
                    COLOR_LOGICAL_WIDTH +
                    x;

                value =
                    (int)image[offset] +
                    error[x + 1];

                if (value < 0)
                    value = 0;

                if (value > 255)
                    value = 255;

                grey =
                    nearest_grey_level(
                        (unsigned)value,
                        levels,
                        level_count);

                image[offset] =
                    (uint8_t)grey;

                diff =
                    value -
                    (int)grey;

                /*
                 * Floyd-Steinberg:
                 *
                 *             current   7/16
                 *       3/16   5/16     1/16
                 */

                error[x + 2] +=
                    (diff * 7) / 16;

                next_error[x] +=
                    (diff * 3) / 16;

                next_error[x + 1] +=
                    (diff * 5) / 16;

                next_error[x + 2] +=
                    (diff * 1) / 16;
            }
        }
        else
        {
            for (x = COLOR_LOGICAL_WIDTH;
                 x-- > 0;)
            {
                size_t offset;
                int value;
                unsigned grey;
                int diff;

                offset =
                    (size_t)y *
                    COLOR_LOGICAL_WIDTH +
                    x;

                value =
                    (int)image[offset] +
                    error[x + 1];

                if (value < 0)
                    value = 0;

                if (value > 255)
                    value = 255;

                grey =
                    nearest_grey_level(
                        (unsigned)value,
                        levels,
                        level_count);

                image[offset] =
                    (uint8_t)grey;

                diff =
                    value -
                    (int)grey;

                /*
                 * Mirrored Floyd-Steinberg:
                 *
                 *       1/16   5/16   3/16
                 *              current
                 *       7/16
                 */

                error[x] +=
                    (diff * 7) / 16;

                next_error[x + 2] +=
                    (diff * 3) / 16;

                next_error[x + 1] +=
                    (diff * 5) / 16;

                next_error[x] +=
                    (diff * 1) / 16;
            }
        }

        /*
         * Next scan line becomes current.
         */
        memcpy(
            error,
            next_error,
            sizeof(error));
    }
}


/*
 * Four-level perceptually weighted grayscale
 * with post-resize Floyd-Steinberg dithering.
 *
 * Pipeline:
 *
 *     ZX 256x192
 *          |
 *          v
 *     smart palette analysis
 *          |
 *          +----> four optimal grey levels
 *          |
 *          v
 *     luminance resize
 *          |
 *          v
 *     160x240 luminance
 *          |
 *          v
 *     Floyd-Steinberg
 *          |
 *          v
 *     160x240 four-level grayscale
 *          |
 *          v
 *     horizontal x2
 *          |
 *          v
 *     320x240 RGB
 */
int convert_scr_to_color_grey_smartd_reference(
    const uint8_t *scr,
    uint8_t *rgb,
    unsigned *unique_colors)
{
    unsigned levels[COLOR_MAX_COLORS];
    unsigned level_count;

    uint8_t *luminance;

    unsigned x;
    unsigned y;

    if (!scr || !rgb)
        return 0;

    /*
     * The palette is calculated from the original
     * 256x192 image, before geometric reduction.
     */
    level_count =
        build_smart_grey_levels(
            scr,
            levels);

    if (level_count == 0)
        return 0;

    /*
     * This is the actual logical Atari image.
     *
     * Dithering MUST operate on this 160x240 grid.
     */
    luminance =
        (uint8_t *)malloc(
            (size_t)COLOR_LOGICAL_WIDTH *
            COLOR_LOGICAL_HEIGHT);

    if (!luminance)
        return 0;

    /*
     * First perform the existing 256x192 -> 160x240
     * nearest-neighbour geometry conversion.
     *
     * No quantization and no dithering here.
     */
    for (y = 0;
         y < COLOR_LOGICAL_HEIGHT;
         ++y)
    {
        unsigned source_y;

        source_y =
            (y * COLOR_SOURCE_HEIGHT +
             COLOR_PHYSICAL_HEIGHT / 2) /
            COLOR_PHYSICAL_HEIGHT;

        if (source_y >= COLOR_SOURCE_HEIGHT)
            source_y =
                COLOR_SOURCE_HEIGHT - 1;

        for (x = 0;
             x < COLOR_LOGICAL_WIDTH;
             ++x)
        {
            unsigned source_x;
            unsigned zx_index;

            source_x =
                (x * COLOR_SOURCE_WIDTH +
                 COLOR_LOGICAL_WIDTH / 2) /
                COLOR_LOGICAL_WIDTH;

            if (source_x >= COLOR_SOURCE_WIDTH)
                source_x =
                    COLOR_SOURCE_WIDTH - 1;

            zx_index =
                zx_pixel_index(
                    scr,
                    source_x,
                    source_y);

            luminance[
                (size_t)y *
                COLOR_LOGICAL_WIDTH +
                x] =
                (uint8_t)
                zx_color_luminance(
                    zx_index);
        }
    }

    /*
     * NOW apply dithering.
     *
     * At this point the image is already 160x240,
     * which is exactly the final logical Atari geometry.
     */
    dither_grey_levels(
        luminance,
        levels,
        level_count);

    /*
     * Finally expand every logical pixel horizontally
     * to two RGB pixels.
     *
     * There is no resize after dithering.
     */
    for (y = 0;
         y < COLOR_PHYSICAL_HEIGHT;
         ++y)
    {
        for (x = 0;
             x < COLOR_LOGICAL_WIDTH;
             ++x)
        {
            unsigned grey;
            size_t out_offset;

            grey =
                luminance[
                    (size_t)y *
                    COLOR_LOGICAL_WIDTH +
                    x];

            out_offset =
                ((size_t)y *
                 COLOR_PHYSICAL_WIDTH +
                 (size_t)x * 2) * 3;

            /*
             * First physical pixel.
             */
            rgb[out_offset + 0] =
                (uint8_t)grey;

            rgb[out_offset + 1] =
                (uint8_t)grey;

            rgb[out_offset + 2] =
                (uint8_t)grey;

            /*
             * Second physical pixel.
             */
            rgb[out_offset + 3] =
                (uint8_t)grey;

            rgb[out_offset + 4] =
                (uint8_t)grey;

            rgb[out_offset + 5] =
                (uint8_t)grey;
        }
    }

    free(luminance);

    if (unique_colors)
        *unique_colors = level_count;

    return 1;
}
