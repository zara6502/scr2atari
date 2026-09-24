#ifndef GTC_FORMAT_H
#define GTC_FORMAT_H

#include <cstdint>

static constexpr uint32_t GTC_MAGIC =
    0x31435447u; // "GTC1"

static constexpr uint32_t GTC_VERSION = 4;

/*
 * Archive versions.
 *
 * v6:
 *     Original archive format.
 *     1 global Huffman model, or legacy
 *     one-Huffman-model-per-file mode.
 *
 * v7:
 *     Compact per-file Huffman model.
 *     Global code lengths + signed deltas.
 */
static constexpr uint32_t GTC_ARCHIVE_VERSION_LEGACY = 6;
static constexpr uint32_t GTC_ARCHIVE_VERSION = 7;

/*
 * Archive Huffman model modes.
 */
static constexpr uint32_t GTC_HUFFMAN_MODE_GLOBAL = 1;
static constexpr uint32_t GTC_HUFFMAN_MODE_PER_FILE = 2;
static constexpr uint32_t GTC_HUFFMAN_MODE_DELTA = 3;

static constexpr uint32_t GTC_BASE_TOKENS = 256;

/*
 * Internal token used only while building a multi-file
 * grammar. It is never written to the final token stream.
 */
static constexpr uint32_t GTC_FILE_BOUNDARY =
    0xFFFFFFFFu;


/*
 * Single-file GTC header.
 */
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
 * Archive header.
 *
 * The first 56 bytes are the old v6 header and remain
 * unchanged.
 *
 * v7 appends:
 *
 *     huffman_model_mode : u32
 *
 * v6:
 *
 *     huffman_model_count == 1
 *         -> global Huffman
 *
 *     huffman_model_count == file_count
 *         -> legacy per-file Huffman
 *
 * v7:
 *
 *     huffman_model_count == 1
 *     huffman_model_mode  == GTC_HUFFMAN_MODE_DELTA
 *
 *     The one stored Huffman table is the global
 *     code-length model. Per-file code lengths are
 *     reconstructed from the signed delta stream.
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

    /*
     * Present only in archive v7.
     */
    uint32_t huffman_model_mode;
};

#endif