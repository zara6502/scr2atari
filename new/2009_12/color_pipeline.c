#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "color_pipeline.h"
#include "scr.h"


/*
 * ============================================================
 * ZX palette
 * ============================================================
 *
 * This is intentionally local to the new pipeline.
 *
 * We keep the original ZX RGB values rather than converting
 * the image to luminance before deciding what it represents.
 */
static const uint8_t pipeline_zx_palette[16][3] =
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
 * ============================================================
 * ZX pixel access
 * ============================================================
 */

static unsigned pipeline_zx_pixel_index(
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

    return
        (bitmap_byte & (0x80u >> (x & 7)))
            ? ink
            : paper;
}


/*
 * ============================================================
 * Bresenham maps
 * ============================================================
 *
 * This is the same algorithm already used by the working
 * -colorn-bres implementation.
 *
 * Do NOT "improve" this code here.
 */

static void build_bres_x_map(
    unsigned map[COLOR_LOGICAL_WIDTH])
{
    unsigned source;
    unsigned error;
    unsigned x;

    source = 0;
    error = 0;

    for (x = 0;
         x < COLOR_LOGICAL_WIDTH;
         ++x)
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


static void build_bres_y_map(
    unsigned map[COLOR_PHYSICAL_HEIGHT])
{
    unsigned source;
    unsigned error;
    unsigned y;

    source = 0;
    error = 0;

    for (y = 0;
         y < COLOR_PHYSICAL_HEIGHT;
         ++y)
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
 * ============================================================
 * Logical source sampling
 * ============================================================
 */

static unsigned sample_source_nearest_x(
    unsigned x)
{
    unsigned source_x;

    source_x =
        (x * COLOR_SOURCE_WIDTH +
         COLOR_LOGICAL_WIDTH / 2) /
        COLOR_LOGICAL_WIDTH;

    if (source_x >= COLOR_SOURCE_WIDTH)
        source_x = COLOR_SOURCE_WIDTH - 1;

    return source_x;
}


static unsigned sample_source_nearest_y(
    unsigned y)
{
    unsigned source_y;

    source_y =
        (y * COLOR_SOURCE_HEIGHT +
         COLOR_PHYSICAL_HEIGHT / 2) /
        COLOR_PHYSICAL_HEIGHT;

    if (source_y >= COLOR_SOURCE_HEIGHT)
        source_y = COLOR_SOURCE_HEIGHT - 1;

    return source_y;
}


/*
 * Get ZX color index for one logical Atari pixel.
 *
 * This is the important abstraction:
 *
 *     source ZX RGB
 *          |
 *          v
 *     logical 160x240 pixel
 *
 * Color model is applied AFTER this point.
 */
static unsigned sample_zx_index(
    const uint8_t *scr,
    unsigned x,
    unsigned y,
    ColorFilter filter,
    const unsigned bres_x[],
    const unsigned bres_y[])
{
    unsigned source_x;
    unsigned source_y;

    if (filter == COLOR_FILTER_BRES)
    {
        source_x = bres_x[x];
        source_y = bres_y[y];
    }
    else
    {
        /*
         * For the first architecture step, nearest is used
         * for the logical source.
         *
         * AREA is intentionally routed through the old,
         * already-tested implementation below.
         */
        source_x =
            sample_source_nearest_x(x);

        source_y =
            sample_source_nearest_y(y);
    }

    return
        pipeline_zx_pixel_index(
            scr,
            source_x,
            source_y);
}


/*
 * ============================================================
 * Grey levels
 * ============================================================
 *
 * For the new pipeline we deliberately do NOT use the old
 * smart/smartd code.
 *
 * The first version simply derives four levels from the
 * actual ZX colors present in the image.
 *
 * Later this function will be replaced by the new MIX-aware
 * global optimizer.
 */

static unsigned zx_luminance(
    unsigned zx_index)
{
    const uint8_t *p =
        pipeline_zx_palette[zx_index & 15];

    return
        (299u * p[0] +
         587u * p[1] +
         114u * p[2] +
         500u) / 1000u;
}


static void build_basic_grey_levels(
    const uint8_t *scr,
    unsigned levels[COLOR_MAX_COLORS],
    unsigned *level_count)
{
    unsigned histogram[256];
    unsigned i;
    unsigned x;
    unsigned y;
    unsigned count;

    memset(
        histogram,
        0,
        sizeof(histogram));

    /*
     * Count actual source luminances.
     *
     * Notice that normal and bright colors are NOT merged.
     */
    for (y = 0;
         y < COLOR_SOURCE_HEIGHT;
         ++y)
    {
        for (x = 0;
             x < COLOR_SOURCE_WIDTH;
             ++x)
        {
            unsigned zx;
            unsigned lum;

            zx =
                pipeline_zx_pixel_index(
                    scr,
                    x,
                    y);

            lum = zx_luminance(zx);

            ++histogram[lum];
        }
    }

    /*
     * Pick the four most frequent luminance values.
     */
    count = 0;

    while (count < COLOR_MAX_COLORS)
    {
        unsigned best;
        unsigned best_count;

        best = 0;
        best_count = 0;

        for (i = 0;
             i < 256;
             ++i)
        {
            if (histogram[i] > best_count)
            {
                best = i;
                best_count = histogram[i];
            }
        }

        if (best_count == 0)
            break;

        levels[count++] = best;

        histogram[best] = 0;
    }

    /*
     * Sort dark -> bright.
     */
    for (i = 0;
         i < count;
         ++i)
    {
        unsigned j;

        for (j = i + 1;
             j < count;
             ++j)
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

    *level_count = count;
}


static unsigned nearest_level(
    unsigned value,
    const unsigned levels[],
    unsigned count)
{
    unsigned best;
    unsigned best_distance;
    unsigned i;

    if (count == 0)
        return 0;

    best = levels[0];

    if (value >= best)
        best_distance = value - best;
    else
        best_distance = best - value;

    for (i = 1;
         i < count;
         ++i)
    {
        unsigned distance;

        if (value >= levels[i])
            distance = value - levels[i];
        else
            distance = levels[i] - value;

        if (distance < best_distance)
        {
            best_distance = distance;
            best = levels[i];
        }
    }

    return best;
}


/*
 * ============================================================
 * New grey pipeline
 * ============================================================
 */

static int convert_grey(
    const uint8_t *scr,
    uint8_t *rgb,
    ColorFilter filter,
    unsigned *unique_colors)
{
    unsigned levels[COLOR_MAX_COLORS];
    unsigned level_count;

    unsigned bres_x[COLOR_LOGICAL_WIDTH];
    unsigned bres_y[COLOR_PHYSICAL_HEIGHT];

    unsigned x;
    unsigned y;

    build_basic_grey_levels(
        scr,
        levels,
        &level_count);

    if (level_count == 0)
        return 0;

    if (filter == COLOR_FILTER_BRES)
    {
        build_bres_x_map(bres_x);
        build_bres_y_map(bres_y);
    }

    for (y = 0;
         y < COLOR_PHYSICAL_HEIGHT;
         ++y)
    {
        for (x = 0;
             x < COLOR_LOGICAL_WIDTH;
             ++x)
        {
            unsigned zx;
            unsigned lum;
            unsigned grey;

            size_t out;

            zx =
                sample_zx_index(
                    scr,
                    x,
                    y,
                    filter,
                    bres_x,
                    bres_y);

            lum =
                zx_luminance(zx);

            grey =
                nearest_level(
                    lum,
                    levels,
                    level_count);

            out =
                ((size_t)y *
                 COLOR_PHYSICAL_WIDTH +
                 (size_t)x * 2) * 3;

            rgb[out + 0] = (uint8_t)grey;
            rgb[out + 1] = (uint8_t)grey;
            rgb[out + 2] = (uint8_t)grey;

            rgb[out + 3] = (uint8_t)grey;
            rgb[out + 4] = (uint8_t)grey;
            rgb[out + 5] = (uint8_t)grey;
        }
    }

    if (unique_colors)
        *unique_colors = level_count;

    return 1;
}


/*
 * ============================================================
 * New pipeline
 * ============================================================
 */

void color_options_init(
    ColorOptions *options)
{
    if (!options)
        return;

    options->model =
        COLOR_MODEL_FIXED;

    options->filter =
        COLOR_FILTER_NEAREST;

    options->mix = 0;
}


int color_pipeline_convert(
    const uint8_t *scr,
    uint8_t *rgb,
    const ColorOptions *options,
    unsigned *unique_colors)
{
    if (!scr || !rgb || !options)
        return 0;

    /*
     * Fixed -color.
     *
     * We deliberately call the existing implementation.
     *
     * Therefore the new architecture does not alter
     * the already-working -color output.
     */
    if (options->model == COLOR_MODEL_FIXED)
    {
        return
            convert_scr_to_color_reference(
                scr,
                rgb,
                0,
                COLOR_RESIZE_NEAREST,
                unique_colors);
    }


    /*
     * Nearest GTIA.
     *
     * Reuse the existing tested implementation.
     *
     * This gives:
     *
     *     -color -nearest
     *     -color -nearest -bres
     *     -color -nearest -area
     *
     * without copying or modifying the old algorithm.
     */
    if (options->model == COLOR_MODEL_NEAREST)
    {
        int resize_mode;

        if (options->filter == COLOR_FILTER_BRES)
            resize_mode = COLOR_RESIZE_BRES;
        else if (options->filter == COLOR_FILTER_AREA)
            resize_mode = COLOR_RESIZE_AREA;
        else
            resize_mode = COLOR_RESIZE_NEAREST;

        return
            convert_scr_to_color_reference(
                scr,
                rgb,
                1,
                resize_mode,
                unique_colors);
    }


    /*
     * New grayscale pipeline.
     *
     * MIX is deliberately accepted here but is not yet
     * materialized. This lets us establish the new CLI and
     * architecture first.
     *
     * The next step replaces convert_grey() with:
     *
     *     4 solids
     *     6 pair MIXes
     *     source-color -> best representation
     *
     * without changing scr2atari.c again.
     */
    if (options->model == COLOR_MODEL_GREY)
    {
        (void)options->mix;

        return
            convert_grey(
                scr,
                rgb,
                options->filter,
                unique_colors);
    }

    return 0;
}