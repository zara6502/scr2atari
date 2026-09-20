#include <stdint.h>
#include <string.h>
#include "atari.h"
#include "scr.h"
#include "xex.h"
#include "filters.h"

/* ========================================================= */
/* Atari memory image                                        */
/* ========================================================= */

void build_atari_memory(const uint8_t *scr,
                               uint8_t *memory,
                               int dithering)
{
    uint8_t screen[SCREEN_SIZE];
    size_t dl_size;

    memset(memory,
           0,
           PROGRAM_END - PROGRAM_START + 1);

    memset(screen, 0, sizeof(screen));

    if (dithering == 3)
        convert_scr_to_atari_ordered(scr, screen);
    else if (dithering)
        convert_scr_to_atari_dither(scr, screen, dithering);
    else
        convert_scr_to_atari(scr, screen);

    build_program(memory);

    dl_size = build_display_list(memory);

    (void)dl_size;

    memcpy(memory + (SCREEN_ADDR - PROGRAM_START),
           screen,
           SCREEN_SIZE);

    write_atari_text(memory,
                     COPYRIGHT_ADDR,
                     "ATARI ZXART VIEWER 2023 ZARA6502",
                     32);

    write_atari_text(memory,
                     GITHUB_ADDR,
                     " github.com/zara6502/scr2atari  ",
                     32);
}

void write_atari_text(uint8_t *memory,
                             unsigned address,
                             const char *text,
                             size_t width)
{
    size_t i;

    for (i = 0; i < width; ++i)
    {
        uint8_t c;

        if (text[i] != '\0')
            c = (uint8_t)text[i];
        else
            c = 32;

        memory[address - PROGRAM_START + i] =
            atascii_to_icode(c);
    }
}

/* ========================================================= */
/* ATASCII -> ICODE                                          */
/* ========================================================= */

uint8_t atascii_to_icode(uint8_t c)
{
    if (c < 32)
        return (uint8_t)(c + 64);

    if (c < 96)
        return (uint8_t)(c - 32);

    if (c < 128)
        return c;

    if (c < 160)
        return (uint8_t)(c + 64);

    if (c < 224)
        return (uint8_t)(c - 32);

    return c;
}
