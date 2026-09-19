#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "scr.h"

/* ========================================================= */
/* ZX Spectrum bitmap / attribute addressing                  */
/* ========================================================= */

static unsigned zx_bitmap_offset(unsigned x, unsigned y)
{
    return
        ((y & 0xC0) << 5) |
        ((y & 0x07) << 8) |
        ((y & 0x38) << 2) |
        (x >> 3);
}

static unsigned zx_attr_offset(unsigned x, unsigned y)
{
    return 6144 + (y >> 3) * 32 + (x >> 3);
}


/* ========================================================= */
/* ZX Spectrum palette                                       */
/* ========================================================= */

void zx_get_palette(int index,
                    uint8_t *r,
                    uint8_t *g,
                    uint8_t *b)
{
    static const uint8_t normal[8][3] =
    {
        {   0,   0,   0 },
        {   0,   0, 205 },
        { 205,   0,   0 },
        { 205,   0, 205 },
        {   0, 205,   0 },
        {   0, 205, 205 },
        { 205, 205,   0 },
        { 205, 205, 205 }
    };

    static const uint8_t bright[8][3] =
    {
        {   0,   0,   0 },
        {   0,   0, 255 },
        { 255,   0,   0 },
        { 255,   0, 255 },
        {   0, 255,   0 },
        {   0, 255, 255 },
        { 255, 255,   0 },
        { 255, 255, 255 }
    };

    const uint8_t (*p)[3];

    if (index & 8)
        p = bright;
    else
        p = normal;

    index &= 7;

    *r = p[index][0];
    *g = p[index][1];
    *b = p[index][2];
}


int zx_color_is_light(unsigned color)
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    int brightness;

    zx_get_palette((int)color, &r, &g, &b);

    brightness =
        (299 * (int)r +
         587 * (int)g +
         114 * (int)b) / 1000;

    return brightness >= 128;
}


/* ========================================================= */
/* ZX -> RGB                                                  */
/* ========================================================= */

void zx_to_rgb(const uint8_t *scr, uint8_t *rgb)
{
    unsigned x;
    unsigned y;

    for (y = 0; y < 192; ++y)
    {
        for (x = 0; x < 256; ++x)
        {
            unsigned bitmap_pos;
            unsigned attr_pos;
            unsigned attr;
            unsigned ink;
            unsigned paper;
            unsigned bright;
            unsigned color;

            uint8_t r;
            uint8_t g;
            uint8_t b;

            bitmap_pos = zx_bitmap_offset(x, y);
            attr_pos   = zx_attr_offset(x, y);

            attr = scr[attr_pos];

            ink    = attr & 7;
            paper  = (attr >> 3) & 7;
            bright = (attr >> 6) & 1;

            if (scr[bitmap_pos] & (0x80 >> (x & 7)))
                color = ink;
            else
                color = paper;

            if (bright)
                color |= 8;

            zx_get_palette((int)color, &r, &g, &b);

            rgb[(y * 256 + x) * 3 + 0] = r;
            rgb[(y * 256 + x) * 3 + 1] = g;
            rgb[(y * 256 + x) * 3 + 2] = b;
        }
    }
}


/* ========================================================= */
/* ZX -> Atari monochrome                                    */
/* ========================================================= */

void convert_scr_to_atari(const uint8_t *scr,
                          uint8_t *screen)
{
    unsigned y;
    unsigned xbyte;

    for (y = 0; y < SCREEN_HEIGHT; ++y)
    {
        for (xbyte = 0; xbyte < 32; ++xbyte)
        {
            unsigned x;
            unsigned attr_pos;
            unsigned bitmap_pos;
            unsigned attr;
            unsigned ink;
            unsigned paper;
            unsigned bright;
            unsigned ink_color;
            unsigned paper_color;

            int ink_light;
            int paper_light;

            uint8_t value;

            x = xbyte * 8;

            attr_pos = zx_attr_offset(x, y);
            attr = scr[attr_pos];

            ink    = attr & 7;
            paper  = (attr >> 3) & 7;
            bright = (attr >> 6) & 1;

            ink_color =
                ink | (bright ? 8 : 0);

            paper_color =
                paper | (bright ? 8 : 0);

            ink_light =
                zx_color_is_light(ink_color);

            paper_light =
                zx_color_is_light(paper_color);

            bitmap_pos =
                zx_bitmap_offset(x, y);

            value = scr[bitmap_pos];

            if (ink_light == paper_light)
            {
                screen[y * 32 + xbyte] =
                    ink_light ? 0xFF : 0x00;
            }
            else if (ink_light)
            {
                screen[y * 32 + xbyte] =
                    value;
            }
            else
            {
                screen[y * 32 + xbyte] =
                    (uint8_t)~value;
            }
        }
    }
}


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


/* ========================================================= */
/* ZX -> Atari color reference image                          */
/*                                                           */
/* 256x192 ZX image                                          */
/*      -> 192x192 logical Atari pixels                      */
/*      -> 384x192 physical/reference image                  */
/*                                                           */
/* At most four different source colors are allowed.         */
/* No dithering. No blending. No color mixing.               */
/* ========================================================= */

#define ATARI_LOGICAL_WIDTH   192
#define ATARI_PHYSICAL_WIDTH  384
#define ATARI_COLOR_COUNT     256
#define ATARI_COLOR_LIMIT     4


static unsigned color_distance_sq(const uint8_t *a,
                                  const uint8_t *b)
{
    int dr = (int)a[0] - (int)b[0];
    int dg = (int)a[1] - (int)b[1];
    int db = (int)a[2] - (int)b[2];

    return (unsigned)(
        dr * dr +
        dg * dg +
        db * db
    );
}


static unsigned nearest_atari_color(const uint8_t *rgb)
{
    unsigned i;
    unsigned best = 0;
    unsigned best_distance = 0xffffffffu;

    for (i = 0; i < ATARI_COLOR_COUNT; ++i)
    {
        unsigned distance =
            color_distance_sq(rgb, atari_gtia_palette[i]);

        if (distance < best_distance)
        {
            best_distance = distance;
            best = i;

            if (distance == 0)
                break;
        }
    }

    return best;
}


int zx_to_atari_color_rgb(const uint8_t *scr,
                          uint8_t *rgb)
{
    uint8_t source[SCREEN_WIDTH * SCREEN_HEIGHT * 3];

    uint8_t unique_rgb[ATARI_COLOR_LIMIT][3];
    unsigned unique_count = 0;

    unsigned color_map[ATARI_COLOR_LIMIT];

    unsigned x;
    unsigned y;

    /*
     * First create the normal 256x192 ZX RGB image.
     */
    zx_to_rgb(scr, source);

    /*
     * Find actual unique source colors.
     *
     * ZX attributes normally produce only a small number of
     * colors, but this is intentionally checked against the
     * complete image rather than against attributes alone.
     */
    for (y = 0; y < SCREEN_HEIGHT; ++y)
    {
        for (x = 0; x < SCREEN_WIDTH; ++x)
        {
            const uint8_t *p =
                &source[(y * SCREEN_WIDTH + x) * 3];

            unsigned i;
            int found = 0;

            for (i = 0; i < unique_count; ++i)
            {
                if (unique_rgb[i][0] == p[0] &&
                    unique_rgb[i][1] == p[1] &&
                    unique_rgb[i][2] == p[2])
                {
                    found = 1;
                    break;
                }
            }

            if (!found)
            {
                if (unique_count >= ATARI_COLOR_LIMIT)
                    return 0;

                unique_rgb[unique_count][0] = p[0];
                unique_rgb[unique_count][1] = p[1];
                unique_rgb[unique_count][2] = p[2];

                ++unique_count;
            }
        }
    }

    /*
     * Map each source color independently to the nearest
     * actual Atari GTIA palette entry.
     */
    for (x = 0; x < unique_count; ++x)
        color_map[x] = nearest_atari_color(unique_rgb[x]);

    /*
     * Convert 256x192 ZX geometry to 192x192 logical Atari
     * geometry.
     *
     * Every logical Atari pixel becomes two horizontal pixels
     * in the reference image.
     */
    for (y = 0; y < SCREEN_HEIGHT; ++y)
    {
        for (x = 0; x < ATARI_LOGICAL_WIDTH; ++x)
        {
            unsigned sx;
            const uint8_t *p;
            unsigned source_color = 0;
            unsigned atari_color;
            unsigned out_x;

            /*
             * Center-sampled 256 -> 192 horizontal mapping.
             */
            sx = (x * SCREEN_WIDTH + SCREEN_WIDTH / 2) /
                 ATARI_LOGICAL_WIDTH;

            if (sx >= SCREEN_WIDTH)
                sx = SCREEN_WIDTH - 1;

            p =
                &source[(y * SCREEN_WIDTH + sx) * 3];

            for (source_color = 0;
                 source_color < unique_count;
                 ++source_color)
            {
                if (unique_rgb[source_color][0] == p[0] &&
                    unique_rgb[source_color][1] == p[1] &&
                    unique_rgb[source_color][2] == p[2])
                {
                    break;
                }
            }

            if (source_color >= unique_count)
                return 0;

            atari_color = color_map[source_color];

            out_x = x * 2;

            rgb[(y * ATARI_PHYSICAL_WIDTH + out_x) * 3 + 0] =
                atari_gtia_palette[atari_color][0];

            rgb[(y * ATARI_PHYSICAL_WIDTH + out_x) * 3 + 1] =
                atari_gtia_palette[atari_color][1];

            rgb[(y * ATARI_PHYSICAL_WIDTH + out_x) * 3 + 2] =
                atari_gtia_palette[atari_color][2];

            rgb[(y * ATARI_PHYSICAL_WIDTH + out_x + 1) * 3 + 0] =
                atari_gtia_palette[atari_color][0];

            rgb[(y * ATARI_PHYSICAL_WIDTH + out_x + 1) * 3 + 1] =
                atari_gtia_palette[atari_color][1];

            rgb[(y * ATARI_PHYSICAL_WIDTH + out_x + 1) * 3 + 2] =
                atari_gtia_palette[atari_color][2];
        }
    }

    return 1;
}