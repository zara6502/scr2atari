#ifndef COLOR_PIPELINE_H
#define COLOR_PIPELINE_H

#include <stdint.h>

#include "color.h"


/*
 * Main color model.
 *
 * COLOR_MODEL_FIXED
 *     Existing fixed -color mapping.
 *
 * COLOR_MODEL_NEAREST
 *     Existing nearest GTIA mapping.
 *
 * COLOR_MODEL_GREY
 *     New four-level grayscale pipeline.
 */
typedef enum
{
    COLOR_MODEL_FIXED = 0,
    COLOR_MODEL_NEAREST,
    COLOR_MODEL_GREY

} ColorModel;


/*
 * Spatial resize/filter.
 */
typedef enum
{
    COLOR_FILTER_NEAREST = 0,
    COLOR_FILTER_BRES,
    COLOR_FILTER_AREA

} ColorFilter;


/*
 * New color pipeline configuration.
 *
 * model:
 *
 *     what representation is used.
 *
 * filter:
 *
 *     how the 256x192 ZX image is sampled into
 *     the 160x240 Atari logical image.
 *
 * mix:
 *
 *     enable MIX representation.
 *
 * NOTE:
 *
 * MIX is deliberately kept as a separate option.
 * It does not create another "-colorn-..." mode.
 */
typedef struct
{
    ColorModel model;
    ColorFilter filter;
    int mix;

} ColorOptions;


/*
 * New color conversion entry point.
 *
 * Output:
 *
 *     320x240 RGB888
 *
 * Returns:
 *
 *     1 = success
 *     0 = error
 */
int color_pipeline_convert(
    const uint8_t *scr,
    uint8_t *rgb,
    const ColorOptions *options,
    unsigned *unique_colors);


/*
 * Convenience initializer.
 */
void color_options_init(
    ColorOptions *options);

#endif