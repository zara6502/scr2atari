#ifndef GTC_FORMAT_H
#define GTC_FORMAT_H

#include <cstdint>

static constexpr uint32_t GTC_MAGIC =
    0x31435447u; // "GTC1"

static constexpr uint32_t GTC_VERSION = 2;

static constexpr uint32_t GTC_BASE_TOKENS = 256;

struct GtcHeader {
    uint32_t magic;
    uint32_t version;

    uint64_t original_size;

    uint32_t token_count;
    uint32_t grammar_count;

    uint64_t token_count_in_stream;

    uint64_t compressed_size;
};

#endif