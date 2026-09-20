/*
 * xex.c
 *
 * Atari XL/XE XEX generation.
 *
 * Builds the working 6502 startup program,
 * ANTIC Display List and final XEX file.
 *
 * The generated Atari startup program and Display List
 * are kept independent from image conversion and dithering.
 */

/* ========================================================= */
/* Atari startup program                                     */
/* ========================================================= */

/*
 * THIS SECTION IS THE WORKING XEX IMPLEMENTATION.
 * It is intentionally kept unchanged.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include "xex.h"

size_t build_program(uint8_t *memory)
{
    size_t p;

    p = 0;

    /*
     * LDA #$30
     * STA SDLSTL
     */

    memory[p++] = 0xA9;
    memory[p++] = 0x30;

    memory[p++] = 0x8D;
    memory[p++] = 0x30;
    memory[p++] = 0x02;

    /*
     * LDA #$20
     * STA SDLSTH
     */

    memory[p++] = 0xA9;
    memory[p++] = 0x20;

    memory[p++] = 0x8D;
    memory[p++] = 0x31;
    memory[p++] = 0x02;

    /*
     * LDA #$21
     * STA SDMCTL
     */

    memory[p++] = 0xA9;
    memory[p++] = 0x21;

    memory[p++] = 0x8D;
    memory[p++] = 0x2F;
    memory[p++] = 0x02;

    /*
     * loop:
     *
     * LDA STRIG0
     * CMP #0
     * BEQ pressed
     */

    memory[p++] = 0xAD;
    memory[p++] = 0x84;
    memory[p++] = 0x02;

    memory[p++] = 0xC9;
    memory[p++] = 0x00;

    memory[p++] = 0xF0;
    memory[p++] = 0x0D;

    /*
     * Released:
     *
     * COLOR2 = $0F
     * COLOR1 = $00
     */

    memory[p++] = 0xA9;
    memory[p++] = 0x00;

    memory[p++] = 0x8D;
    memory[p++] = 0xC6;
    memory[p++] = 0x02;

    memory[p++] = 0xA9;
    memory[p++] = 0x0F;

    memory[p++] = 0x8D;
    memory[p++] = 0xC5;
    memory[p++] = 0x02;

    /*
     * JMP loop
     */

    memory[p++] = 0x4C;
    memory[p++] = 0x0F;
    memory[p++] = 0x20;

    /*
     * pressed:
     *
     * COLOR1 = $00
     * COLOR2 = $0F
     */

    memory[p++] = 0xA9;
    memory[p++] = 0x00;

    memory[p++] = 0x8D;
    memory[p++] = 0xC5;
    memory[p++] = 0x02;

    memory[p++] = 0xA9;
    memory[p++] = 0x0F;

    memory[p++] = 0x8D;
    memory[p++] = 0xC6;
    memory[p++] = 0x02;

    /*
     * JMP loop
     */

    memory[p++] = 0x4C;
    memory[p++] = 0x0F;
    memory[p++] = 0x20;

    return p;
}


/* --------------------------------------------------------- */
/* Atari display list                                       */
/* --------------------------------------------------------- */

size_t build_display_list(uint8_t *memory)
{
    uint8_t *dl;
    size_t p;
    unsigned i;

    dl = memory + (DLIST_ADDR - PROGRAM_START);
    p = 0;

    /*
     * Two single-scanline blank lines.
     */

    dl[p++] = 0x70;
    dl[p++] = 0x70;

    /*
     * ANTIC mode 2 + LMS -> GITHUB_ADDR
     *
     * One text line = 8 scanlines.
     */

    dl[p++] = 0x42;
    dl[p++] = (uint8_t)(GITHUB_ADDR & 255);
    dl[p++] = (uint8_t)(GITHUB_ADDR >> 8);

    /*
     * One single-scanline blank line.
     */

    dl[p++] = 0x00;

    /*
     * First picture block: 119 lines.
     *
     * 119 * 32 = 3808 = $EE0
     *
     * $2120 + $EE0 = $3000
     */

    dl[p++] = 0x4F;
    dl[p++] = (uint8_t)(SCREEN_ADDR & 255);
    dl[p++] = (uint8_t)(SCREEN_ADDR >> 8);

    for (i = 1; i < 119; ++i)
        dl[p++] = 0x0F;

    /*
     * Second picture block: 73 lines.
     *
     * New LMS at $3000 is required.
     */

    dl[p++] = 0x4F;
    dl[p++] = 0x00;
    dl[p++] = 0x30;

    for (i = 1; i < 73; ++i)
        dl[p++] = 0x0F;

    /*
     * One single-scanline blank line.
     */

    dl[p++] = 0x00;

    /*
     * Copyright text line.
     */

    dl[p++] = 0x42;
    dl[p++] = (uint8_t)(COPYRIGHT_ADDR & 255);
    dl[p++] = (uint8_t)(COPYRIGHT_ADDR >> 8);

    /*
     * JVB $2030
     */

    dl[p++] = 0x41;
    dl[p++] = (uint8_t)(DLIST_ADDR & 255);
    dl[p++] = (uint8_t)(DLIST_ADDR >> 8);

    return p;
}

/* ========================================================= */
/* Atari XEX                                                 */
/* ========================================================= */

void put16(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)(value & 255);
    p[1] = (uint8_t)((value >> 8) & 255);
}

int write_xex(const char *filename,
                     const uint8_t *memory)
{
    FILE *f;
    uint8_t header[6];
    uint8_t run[6];
    size_t size;

    f = fopen(filename, "wb");

    if (!f)
        return 0;

    size =
        PROGRAM_END - PROGRAM_START + 1;

    header[0] = 0xFF;
    header[1] = 0xFF;

    put16(header + 2, PROGRAM_START);
    put16(header + 4, PROGRAM_END);

    if (fwrite(header, 1, 6, f) != 6)
    {
        fclose(f);
        return 0;
    }

    if (fwrite(memory, 1, size, f) != size)
    {
        fclose(f);
        return 0;
    }

    run[0] = 0xFF;
    run[1] = 0xFF;

    put16(run + 2, RUNAD);
    put16(run + 4, PROGRAM_START);

    if (fwrite(run, 1, 6, f) != 6)
    {
        fclose(f);
        return 0;
    }

    fclose(f);

    return 1;
}
