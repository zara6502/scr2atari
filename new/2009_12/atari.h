#ifndef ATARI_H
#define ATARI_H

#define PROGRAM_START   0x2000
#define DLIST_ADDR      0x2030
#define ZERO_DATA_ADDR  0x2100
#define SCREEN_ADDR     0x2120
#define COPYRIGHT_ADDR  0x3920
#define GITHUB_ADDR     0x3940
#define PROGRAM_END     0x395F

#define RUNAD           0x02E0

#define SDLSTL          0x0230
#define SDMCTL          0x022F
#define STRIG0          0x0284
#define CR_CHR          709
#define CR_BG           710

void build_atari_memory(const uint8_t *scr, uint8_t *memory, int dithering);
void write_atari_text(uint8_t *memory, unsigned address, const char *text, size_t width);
uint8_t atascii_to_icode(uint8_t c);

#endif