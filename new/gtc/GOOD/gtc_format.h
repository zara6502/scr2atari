#ifndef GTC_FORMAT_H
#define GTC_FORMAT_H

#include <cstdint>

static constexpr uint32_t GTC_MAGIC =
    0x31435447u; // "GTC1"

static constexpr uint32_t GTC_VERSION = 4;
static constexpr uint32_t GTC_ARCHIVE_VERSION = 6;

static constexpr uint32_t GTC_BASE_TOKENS = 256;

/*
 * Internal token used only while building a multi-file
 * grammar. It is never written to the final token stream.
 */
static constexpr uint32_t GTC_FILE_BOUNDARY =
    0xFFFFFFFFu;

struct GtcHeader {
    uint32_t magic;
    uint32_t version;

    uint64_t original_size;

    uint32_t token_count;
    uint32_t grammar_count;

    uint64_t token_count_in_stream;

    uint64_t compressed_size;
};


/*
 * Version 6 archive header.
 *
 * huffman_model_count:
 *
 *     1           = one global Huffman model
 *     file_count  = one Huffman model per file
 *
 * The latter is the experimental "per-file Huffman"
 * archive mode.
 */
struct GtcArchiveHeader {
    uint32_t magic;
    uint32_t version;

    uint64_t original_size;

    uint32_t token_count;
    uint32_t grammar_count;

    uint64_t token_count_in_stream;

    uint64_t compressed_size;

    uint32_t file_count;
    uint32_t index_size;
    uint32_t names_size;

    uint32_t huffman_model_count;
};

#endif