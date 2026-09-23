#include "gtc_encoder.h"
#include "gtc_format.h"
#include "gtc_huffman.h"

#include <cstdio>
#include <fstream>
#include <unordered_map>

namespace {

static uint64_t pairKey(
    uint32_t a,
    uint32_t b)
{
    return
        (static_cast<uint64_t>(a) << 32) |
        b;
}

struct PairInfo {
    uint32_t left = 0;
    uint32_t right = 0;
    uint32_t count = 0;
};

void writeU16(
    std::ofstream& out,
    uint16_t value)
{
    uint8_t b[2];

    b[0] =
        static_cast<uint8_t>(value);

    b[1] =
        static_cast<uint8_t>(value >> 8);

    out.write(
        reinterpret_cast<const char*>(b),
        2);
}

void writeU32(
    std::ofstream& out,
    uint32_t value)
{
    uint8_t b[4];

    b[0] =
        static_cast<uint8_t>(value);

    b[1] =
        static_cast<uint8_t>(value >> 8);

    b[2] =
        static_cast<uint8_t>(value >> 16);

    b[3] =
        static_cast<uint8_t>(value >> 24);

    out.write(
        reinterpret_cast<const char*>(b),
        4);
}

void writeU64(
    std::ofstream& out,
    uint64_t value)
{
    uint8_t b[8];

    for (int i = 0; i < 8; ++i)
        b[i] =
            static_cast<uint8_t>(
                value >> (i * 8));

    out.write(
        reinterpret_cast<const char*>(b),
        8);
}

} // namespace

bool GtcEncoder::readFile(
    const std::string& filename,
    std::vector<uint8_t>& data)
{
    std::ifstream file(
        filename,
        std::ios::binary | std::ios::ate);

    if (!file)
        return false;

    std::streamsize size =
        file.tellg();

    if (size < 0)
        return false;

    file.seekg(0);

    data.resize(
        static_cast<size_t>(size));

    if (size != 0) {
        if (!file.read(
                reinterpret_cast<char*>(data.data()),
                size))
            return false;
    }

    return true;
}

void GtcEncoder::buildGrammar(
    const std::vector<uint8_t>& input,
    GtcDictionary& dictionary,
    std::vector<uint32_t>& sequence)
{
    sequence.clear();

    sequence.reserve(
        input.size());

    for (uint8_t c : input)
        sequence.push_back(c);

    /*
     * Initial GTC prototype:
     *
     * 0..255 = literal bytes
     * 256+   = grammar rules
     *
     * Each iteration finds the most frequent
     * non-overlapping adjacent token pair and
     * replaces it with a new token.
     *
     * This grammar builder is intentionally
     * unchanged.
     */

    while (sequence.size() >= 2) {
        std::unordered_map<
            uint64_t,
            PairInfo> pairs;

        pairs.reserve(
            sequence.size());

        for (size_t i = 0;
             i + 1 < sequence.size();
             ++i) {

            uint32_t a =
                sequence[i];

            uint32_t b =
                sequence[i + 1];

            uint64_t key =
                pairKey(a, b);

            auto it =
                pairs.find(key);

            if (it == pairs.end()) {
                PairInfo info;

                info.left = a;
                info.right = b;
                info.count = 1;

                pairs.emplace(
                    key,
                    info);
            }
            else {
                ++it->second.count;
            }
        }

        PairInfo best;

        for (const auto& item : pairs) {
            const PairInfo& info =
                item.second;

            if (info.count > best.count)
                best = info;
        }

        /*
         * Three occurrences is the minimum useful
         * threshold for this first prototype.
         */
        if (best.count < 3)
            break;

        uint32_t newToken =
            dictionary.addRule(
                best.left,
                best.right);

        std::vector<uint32_t> next;

        next.reserve(
            sequence.size() -
            best.count);

        size_t i = 0;

        while (i < sequence.size()) {
            if (i + 1 < sequence.size() &&
                sequence[i] == best.left &&
                sequence[i + 1] == best.right) {

                next.push_back(newToken);

                i += 2;
            }
            else {
                next.push_back(
                    sequence[i]);

                ++i;
            }
        }

        sequence.swap(next);
    }
}

bool GtcEncoder::writeGtc(
    const std::string& filename,
    uint64_t originalSize,
    const GtcDictionary& dictionary,
    const std::vector<uint32_t>& sequence,
    GtcEncodeStats& stats)
{
    const uint32_t tokenCount =
        GTC_BASE_TOKENS +
        dictionary.size();

    std::vector<uint64_t> frequencies(
        tokenCount,
        0);

    for (uint32_t token : sequence) {
        if (token >= tokenCount)
            return false;

        ++frequencies[token];
    }

    GtcHuffman huffman;

    if (!huffman.build(frequencies))
        return false;

    BitWriter writer;

    huffman.encode(
        sequence,
        writer);

    writer.flush();

    const std::vector<uint8_t>&
        compressed =
            writer.data();

    std::ofstream out(
        filename,
        std::ios::binary);

    if (!out)
        return false;

    GtcHeader header{};

    header.magic =
        GTC_MAGIC;

    header.version =
        GTC_VERSION;

    header.original_size =
        originalSize;

    header.token_count =
        tokenCount;

    header.grammar_count =
        dictionary.size();

    header.token_count_in_stream =
        sequence.size();

    header.compressed_size =
        compressed.size();

    /*
     * Header.
     */
    writeU32(
        out,
        header.magic);

    writeU32(
        out,
        header.version);

    writeU64(
        out,
        header.original_size);

    writeU32(
        out,
        header.token_count);

    writeU32(
        out,
        header.grammar_count);

    writeU64(
        out,
        header.token_count_in_stream);

    writeU64(
        out,
        header.compressed_size);

    /*
     * Grammar.
     */
    for (uint32_t i = 0;
         i < dictionary.size();
         ++i) {

        const GtcRule& rule =
            dictionary.rule(
                256u + i);

        writeU16(
            out,
            rule.left);

        writeU16(
            out,
            rule.right);
    }

    /*
     * Huffman code lengths.
     *
     * GTC2 stores uint16_t per token.
     */
    const std::vector<uint16_t>&
        lengths =
            huffman.codeLengths();

    for (uint16_t length : lengths)
        writeU16(
            out,
            length);

    /*
     * Number of valid bits in the final byte.
     */
    writeU64(
        out,
        writer.bitCount());

    /*
     * Compressed token stream.
     */
    if (!compressed.empty()) {
        out.write(
            reinterpret_cast<const char*>(
                compressed.data()),
            static_cast<std::streamsize>(
                compressed.size()));
    }

    if (!out)
        return false;

    stats.originalSize =
        originalSize;

    stats.finalTokenCount =
        sequence.size();

    stats.tokenCount =
        tokenCount;

    stats.grammarCount =
        dictionary.size();

    stats.compressedBits =
        writer.bitCount();

    stats.compressedBytes =
        compressed.size();

    return true;
}

bool GtcEncoder::encodeFile(
    const std::string& inputName,
    const std::string& outputName,
    GtcEncodeStats& stats)
{
    std::vector<uint8_t> input;

    if (!readFile(
            inputName,
            input)) {

        std::fprintf(
            stderr,
            "error: cannot read %s\n",
            inputName.c_str());

        return false;
    }

    GtcDictionary dictionary;

    std::vector<uint32_t> sequence;

    buildGrammar(
        input,
        dictionary,
        sequence);

    if (!writeGtc(
            outputName,
            input.size(),
            dictionary,
            sequence,
            stats)) {

        std::fprintf(
            stderr,
            "error: cannot write %s\n",
            outputName.c_str());

        return false;
    }

    return true;
}
