#ifndef GTC_TABLE_H
#define GTC_TABLE_H

#include "gtc_huffman.h"

#include <cstdint>
#include <vector>

struct GtcTableStats {
    uint64_t bitCount = 0;
    uint32_t byteCount = 0;
    uint32_t segmentCount = 0;
};

class GtcLengthTable {
public:
    bool encode(
        const std::vector<uint16_t>& lengths,
        std::vector<uint8_t>& output,
        GtcTableStats& stats) const;

    bool decode(
        const std::vector<uint8_t>& input,
        uint32_t tokenCount,
        std::vector<uint16_t>& lengths) const;
};

#endif