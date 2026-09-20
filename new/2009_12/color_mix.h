#ifndef COLOR_MIX_H
#define COLOR_MIX_H

#include <stdint.h>

#include "color_pipeline.h"

int convert_color_grey_mix_pipeline(
    const uint8_t *scr,
    uint8_t *rgb,
    const ColorOptions *options,
    unsigned *unique_colors
);

#endif