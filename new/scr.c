#include <stdint.h>
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
