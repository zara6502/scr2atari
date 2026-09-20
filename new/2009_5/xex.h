#ifndef XEX_H
#define XEX_H

#include <stddef.h>
#include <stdint.h>

#define PROGRAM_START  0x2000
#define DLIST_ADDR     0x2030
#define SCREEN_ADDR    0x2120
#define COPYRIGHT_ADDR 0x3920
#define GITHUB_ADDR    0x3940
#define PROGRAM_END    0x395F
#define RUNAD          0x02E0

size_t build_program(uint8_t *memory);
size_t build_display_list(uint8_t *memory);
void put16(uint8_t *p, unsigned value);
int write_xex(const char *filename, const uint8_t *memory);

#endif