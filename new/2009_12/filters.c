/*
 * filters.c
 *
 * Dithering filters for scr2atari.
 */

#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include "scr.h"

/* ------------------------------------------------------------------------- */
/* ZX Spectrum palette                                                       */
/* ------------------------------------------------------------------------- */

static const uint8_t zx_palette[16][3] = {
    {   0,   0,   0 }, {   0,   0, 192 }, { 192,   0,   0 },
    { 192,   0, 192 }, {   0, 192,   0 }, {   0, 192, 192 },
    { 192, 192,   0 }, { 192, 192, 192 },

    {   0,   0,   0 }, {   0,   0, 255 }, { 255,   0,   0 },
    { 255,   0, 255 }, {   0, 255,   0 }, {   0, 255, 255 },
    { 255, 255,   0 }, { 255, 255, 255 }
};

/* ------------------------------------------------------------------------- */
/* ZX helpers                                                                */
/* ------------------------------------------------------------------------- */

static inline int zx_luminance(int color)
{
    const uint8_t *p = zx_palette[color & 15];

    return (299 * p[0] +
            587 * p[1] +
            114 * p[2]) / 1000;
}

static inline int zx_pixel_offset(int x, int y)
{
    return ((y & 0xC0) << 5) |
           ((y & 0x07) << 8) |
           ((y & 0x38) << 2) |
           (x >> 3);
}

static inline int zx_attr_offset(int x, int y)
{
    return BITMAP_SIZE +
           (y >> 3) * 32 +
           (x >> 3);
}

/* ------------------------------------------------------------------------- */
/* Floyd-Steinberg                                                            */
/* ------------------------------------------------------------------------- */

void convert_scr_to_atari_dither(
    const uint8_t *scr,
    uint8_t *screen,
    int dithering)
{
    int16_t *err_curr;
    int16_t *err_next;

    for (int i = 0; i < SCREEN_SIZE; ++i)
        screen[i] = 0;

    err_curr = (int16_t *)calloc(SCREEN_WIDTH, sizeof(int16_t));
    err_next = (int16_t *)calloc(SCREEN_WIDTH, sizeof(int16_t));

    if (!err_curr || !err_next) {
        free(err_curr);
        free(err_next);

        for (int y = 0; y < SCREEN_HEIGHT; ++y) {
            for (int x = 0; x < SCREEN_WIDTH; ++x) {
                int bo = zx_pixel_offset(x, y);
                int ao = zx_attr_offset(x, y);

                uint8_t bitmap = scr[bo];
                uint8_t attr = scr[ao];

                int ink = attr & 7;
                int paper = (attr >> 3) & 7;

                if (attr & 64) {
                    ink += 8;
                    paper += 8;
                }

                {
                    int color =
                        (bitmap & (0x80 >> (x & 7)))
                        ? ink : paper;

                    if (dithering == 2 && (color & 7) == 7) {
                        screen[y * 32 + (x >> 3)] |=
                            (uint8_t)(0x80 >> (x & 7));
                    }
                    else if (zx_luminance(color) >= 128) {
                        screen[y * 32 + (x >> 3)] |=
                            (uint8_t)(0x80 >> (x & 7));
                    }
                }
            }
        }

        return;
    }

    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            int bo = zx_pixel_offset(x, y);
            int ao = zx_attr_offset(x, y);

            uint8_t bitmap = scr[bo];
            uint8_t attr = scr[ao];

            int ink = attr & 7;
            int paper = (attr >> 3) & 7;

            if (attr & 64) {
                ink += 8;
                paper += 8;
            }

            {
                int color =
                    (bitmap & (0x80 >> (x & 7)))
                    ? ink : paper;

                int lum = zx_luminance(color);

                if (dithering == 2 && (color & 7) == 7) {
                    screen[y * 32 + (x >> 3)] |=
                        (uint8_t)(0x80 >> (x & 7));
                    continue;
                }

                {
                    int corrected = lum + err_curr[x] / 16;
                    int error;

                    if (corrected < 0)
                        corrected = 0;
                    else if (corrected > 255)
                        corrected = 255;

                    if (corrected >= 128) {
                        error = (corrected - 255) * 16;

                        screen[y * 32 + (x >> 3)] |=
                            (uint8_t)(0x80 >> (x & 7));
                    }
                    else {
                        error = corrected * 16;
                    }

                    if (x + 1 < SCREEN_WIDTH)
                        err_curr[x + 1] += error * 7 / 16;

                    if (y + 1 < SCREEN_HEIGHT) {
                        if (x > 0)
                            err_next[x - 1] += error * 3 / 16;

                        err_next[x] += error * 5 / 16;

                        if (x + 1 < SCREEN_WIDTH)
                            err_next[x + 1] += error / 16;
                    }
                }
            }
        }

        {
            int16_t *tmp = err_curr;
            err_curr = err_next;
            err_next = tmp;
        }

        for (int x = 0; x < SCREEN_WIDTH; ++x)
            err_next[x] = 0;
    }

    free(err_curr);
    free(err_next);
}

/* ------------------------------------------------------------------------- */
/* Fine diagonal pattern                                                     */
/* ------------------------------------------------------------------------- */

/*
 * Small 4x4 patterns.
 *
 * Unlike the previous 8x8 matrices these are used only as a subtle
 * perturbation of the threshold. They are NOT used as the actual
 * dithering pattern.
 */

static const uint8_t diagonal4_a[16] = {
     0,  9,  2, 11,
    13,  4, 15,  6,
     3, 12,  1, 10,
    14,  7,  8,  5
};

static const uint8_t diagonal4_b[16] = {
     5, 14,  7,  8,
    10,  1, 12,  3,
     9,  4, 15,  2,
     0, 11,  6, 13
};

static const uint8_t diagonal4_c[16] = {
     0, 13,  6,  9,
    11,  4, 15,  2,
     7, 10,  1, 12,
    14,  3,  8,  5
};

static const uint8_t diagonal4_d[16] = {
     5,  8,  1, 14,
    12,  3, 10,  7,
     9,  0, 13,  4,
     2, 15,  6, 11
};

static inline const uint8_t *diagonal_pattern(int color)
{
    switch (color & 3) {
        case 0:  return diagonal4_a;
        case 1:  return diagonal4_b;
        case 2:  return diagonal4_c;
        default: return diagonal4_d;
    }
}

/*
 * Return a small signed threshold perturbation.
 *
 * Range: approximately -6 .. +6.
 *
 * The perturbation is deliberately weak. Its purpose is only to break
 * the very regular appearance of pure Floyd-Steinberg dithering.
 */
static inline int diagonal_bias(int x, int y, int color)
{
    const uint8_t *p = diagonal_pattern(color);
    int v = p[(y & 3) * 4 + (x & 3)];

    return v - 7;
}

/* ------------------------------------------------------------------------- */
/* Nonlinear color density                                                   */
/* ------------------------------------------------------------------------- */

static inline int color_density(int color)
{
    static int density[16];
    static int initialized = 0;

    if (!initialized) {
        for (int c = 0; c < 16; ++c) {
            int lum = zx_luminance(c);

            if (lum <= 0) {
                density[c] = 0;
            }
            else if (lum >= 255) {
                density[c] = 64;
            }
            else {
                double n = (double)lum / 255.0;
                double d = pow(n, 0.45);

                int v = (int)(d * 64.0 + 0.5);

                if (v < 0)
                    v = 0;
                else if (v > 64)
                    v = 64;

                density[c] = v;
            }
        }

        initialized = 1;
    }

    return density[color & 15];
}

/* ------------------------------------------------------------------------- */
/* Adaptive ordered / error diffusion                                        */
/* ------------------------------------------------------------------------- */

/*
 * The ordered component is intentionally weak.
 *
 * We use a small local threshold perturbation instead of replacing
 * Floyd-Steinberg. This avoids the large-scale checker/stripe structures
 * produced by a pure 4x4 or 8x8 ordered pattern.
 *
 * Low local contrast:
 *     stronger ordered component -> cleaner flat areas.
 *
 * High local contrast:
 *     almost pure Floyd-Steinberg -> preserves edges and details.
 */

void convert_scr_to_atari_ordered(
    const uint8_t *scr,
    uint8_t *screen)
{
    int16_t *err_curr;
    int16_t *err_next;

    for (int i = 0; i < SCREEN_SIZE; ++i)
        screen[i] = 0;

    err_curr = (int16_t *)calloc(SCREEN_WIDTH, sizeof(int16_t));
    err_next = (int16_t *)calloc(SCREEN_WIDTH, sizeof(int16_t));

    /*
     * Allocation fallback.
     *
     * In the unlikely event of allocation failure, use ordinary
     * luminance thresholding.
     */
    if (!err_curr || !err_next) {
        free(err_curr);
        free(err_next);

        for (int y = 0; y < SCREEN_HEIGHT; ++y) {
            for (int x = 0; x < SCREEN_WIDTH; ++x) {
                int bo = zx_pixel_offset(x, y);
                int ao = zx_attr_offset(x, y);

                uint8_t bitmap = scr[bo];
                uint8_t attr = scr[ao];

                int ink = attr & 7;
                int paper = (attr >> 3) & 7;

                if (attr & 64) {
                    ink += 8;
                    paper += 8;
                }

                {
                    int color =
                        (bitmap & (0x80 >> (x & 7)))
                        ? ink : paper;

                    if (zx_luminance(color) >= 128)
                        screen[y * 32 + (x >> 3)] |=
                            (uint8_t)(0x80 >> (x & 7));
                }
            }
        }

        return;
    }

    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            int bo = zx_pixel_offset(x, y);
            int ao = zx_attr_offset(x, y);

            uint8_t bitmap = scr[bo];
            uint8_t attr = scr[ao];

            int ink = attr & 7;
            int paper = (attr >> 3) & 7;

            if (attr & 64) {
                ink += 8;
                paper += 8;
            }

            {
                int color =
                    (bitmap & (0x80 >> (x & 7)))
                    ? ink : paper;

                int lum = zx_luminance(color);

                /*
                 * ZX white stays completely white.
                 */
                if ((color & 7) == 7) {
                    screen[y * 32 + (x >> 3)] |=
                        (uint8_t)(0x80 >> (x & 7));
                    continue;
                }

                /*
                 * Estimate local luminance variation.
                 *
                 * Since the source is ZX Spectrum graphics, sampling
                 * neighboring source pixels is cheap and sufficient.
                 */
                int contrast = 0;

                if (x > 0) {
                    int px = x - 1;
                    int pbo = zx_pixel_offset(px, y);
                    int pao = zx_attr_offset(px, y);
                    uint8_t pb = scr[pbo];
                    uint8_t pa = scr[pao];

                    int pi = pa & 7;
                    int pp = (pa >> 3) & 7;

                    if (pa & 64) {
                        pi += 8;
                        pp += 8;
                    }

                    int pc = (pb & (0x80 >> (px & 7))) ? pi : pp;

                    contrast += abs(lum - zx_luminance(pc));
                }

                if (x + 1 < SCREEN_WIDTH) {
                    int px = x + 1;
                    int pbo = zx_pixel_offset(px, y);
                    int pao = zx_attr_offset(px, y);
                    uint8_t pb = scr[pbo];
                    uint8_t pa = scr[pao];

                    int pi = pa & 7;
                    int pp = (pa >> 3) & 7;

                    if (pa & 64) {
                        pi += 8;
                        pp += 8;
                    }

                    int pc = (pb & (0x80 >> (px & 7))) ? pi : pp;

                    contrast += abs(lum - zx_luminance(pc));
                }

                /*
                 * Ordered bias:
                 *
                 *   flat area      : up to about +/-6
                 *   strong edge    : close to zero
                 */
                int bias = diagonal_bias(x, y, color);

                if (contrast > 120)
                    bias = 0;
                else if (contrast > 60)
                    bias /= 2;

                /*
                 * Error diffusion.
                 *
                 * The diagonal bias changes the effective threshold only
                 * slightly, so it cannot form a visible large-scale grid.
                 */
                {
                    int corrected = lum + err_curr[x] / 16;
                    int threshold = 128 + bias;
                    int error;

                    if (corrected < 0)
                        corrected = 0;
                    else if (corrected > 255)
                        corrected = 255;

                    if (corrected >= threshold) {
                        error = (corrected - 255) * 16;

                        screen[y * 32 + (x >> 3)] |=
                            (uint8_t)(0x80 >> (x & 7));
                    }
                    else {
                        error = corrected * 16;
                    }

                    if (x + 1 < SCREEN_WIDTH)
                        err_curr[x + 1] += error * 7 / 16;

                    if (y + 1 < SCREEN_HEIGHT) {
                        if (x > 0)
                            err_next[x - 1] += error * 3 / 16;

                        err_next[x] += error * 5 / 16;

                        if (x + 1 < SCREEN_WIDTH)
                            err_next[x + 1] += error / 16;
                    }
                }
            }
        }

        {
            int16_t *tmp = err_curr;
            err_curr = err_next;
            err_next = tmp;
        }

        for (int x = 0; x < SCREEN_WIDTH; ++x)
            err_next[x] = 0;
    }

    free(err_curr);
    free(err_next);
}