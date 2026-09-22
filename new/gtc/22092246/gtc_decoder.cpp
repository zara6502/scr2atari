#include "gtc_decoder.h"
#include "gtc_format.h"
#include "gtc_huffman.h"

#include <cstddef>
#include <fstream>
#include <iostream>
#include <vector>

namespace {


struct Rule {
    uint32_t left;
    uint32_t right;
};


bool readFile(
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
                reinterpret_cast<char*>(
                    data.data()),
                size))
            return false;
    }

    return true;
}


bool writeFile(
    const std::string& filename,
    const std::vector<uint8_t>& data)
{
    std::ofstream file(
        filename,
        std::ios::binary);

    if (!file)
        return false;

    if (!data.empty()) {
        file.write(
            reinterpret_cast<const char*>(
                data.data()),
            static_cast<std::streamsize>(
                data.size()));
    }

    return static_cast<bool>(file);
}


bool readU16(
    const std::vector<uint8_t>& data,
    size_t& pos,
    uint16_t& value)
{
    if (pos + 2 > data.size())
        return false;

    value =
        static_cast<uint16_t>(
            data[pos]) |
        static_cast<uint16_t>(
            data[pos + 1] << 8);

    pos += 2;

    return true;
}


bool readU32(
    const std::vector<uint8_t>& data,
    size_t& pos,
    uint32_t& value)
{
    if (pos + 4 > data.size())
        return false;

    value =
        static_cast<uint32_t>(
            data[pos]) |
        (static_cast<uint32_t>(
            data[pos + 1]) << 8) |
        (static_cast<uint32_t>(
            data[pos + 2]) << 16) |
        (static_cast<uint32_t>(
            data[pos + 3]) << 24);

    pos += 4;

    return true;
}


bool readU64(
    const std::vector<uint8_t>& data,
    size_t& pos,
    uint64_t& value)
{
    if (pos + 8 > data.size())
        return false;

    value = 0;

    for (int i = 0; i < 8; ++i) {
        value |=
            static_cast<uint64_t>(
                data[pos + i])
            << (i * 8);
    }

    pos += 8;

    return true;
}


class GrammarExpander {
public:
    GrammarExpander(
        const std::vector<Rule>& rules,
        std::vector<uint8_t>& output)
        : rules_(rules),
          output_(output)
    {
    }


    void expand(uint32_t token)
    {
        /*
         * Iterative DFS instead of recursive
         * C++ calls.
         *
         * This avoids stack overflow for deeply
         * nested grammars.
         */
        stack_.clear();

        stack_.push_back(token);

        while (!stack_.empty()) {
            uint32_t current =
                stack_.back();

            stack_.pop_back();

            if (current < 256) {
                output_.push_back(
                    static_cast<uint8_t>(
                        current));

                continue;
            }

            uint32_t index =
                current - 256;

            if (index >= rules_.size())
                return;

            const Rule& rule =
                rules_[index];

            /*
             * Reverse order because the stack
             * is LIFO.
             */
            stack_.push_back(
                rule.right);

            stack_.push_back(
                rule.left);
        }
    }


private:
    const std::vector<Rule>& rules_;

    std::vector<uint8_t>& output_;

    std::vector<uint32_t> stack_;
};


} // namespace


bool GtcDecoder::readU32(
    const std::vector<uint8_t>& data,
    size_t& pos,
    uint32_t& value)
{
    return ::readU32(
        data,
        pos,
        value);
}


bool GtcDecoder::readU64(
    const std::vector<uint8_t>& data,
    size_t& pos,
    uint64_t& value)
{
    return ::readU64(
        data,
        pos,
        value);
}


bool GtcDecoder::decodeFile(
    const std::string& inputName,
    const std::string& outputName)
{
    std::vector<uint8_t> data;

    if (!readFile(
            inputName,
            data)) {

        std::cerr
            << "error: cannot read "
            << inputName
            << "\n";

        return false;
    }

    size_t pos = 0;

    uint32_t magic;
    uint32_t version;

    uint64_t originalSize;

    uint32_t tokenCount;
    uint32_t grammarCount;

    uint64_t tokenCountInStream;
    uint64_t compressedSize;

    if (!readU32(
            data,
            pos,
            magic) ||

        !readU32(
            data,
            pos,
            version) ||

        !readU64(
            data,
            pos,
            originalSize) ||

        !readU32(
            data,
            pos,
            tokenCount) ||

        !readU32(
            data,
            pos,
            grammarCount) ||

        !readU64(
            data,
            pos,
            tokenCountInStream) ||

        !readU64(
            data,
            pos,
            compressedSize)) {

        std::cerr
            << "error: truncated header\n";

        return false;
    }

    if (magic != GTC_MAGIC) {
        std::cerr
            << "error: invalid GTC file\n";

        return false;
    }

    if (version != GTC_VERSION) {
        std::cerr
            << "error: unsupported GTC version\n";

        return false;
    }

    if (tokenCount !=
        GTC_BASE_TOKENS +
        grammarCount) {

        std::cerr
            << "error: invalid token count\n";

        return false;
    }

    std::vector<Rule> rules(
        grammarCount);

    for (uint32_t i = 0;
         i < grammarCount;
         ++i) {

        if (!readU32(
                data,
                pos,
                rules[i].left) ||

            !readU32(
                data,
                pos,
                rules[i].right)) {

            std::cerr
                << "error: truncated grammar\n";

            return false;
        }

        if (rules[i].left >= tokenCount ||
            rules[i].right >= tokenCount) {

            std::cerr
                << "error: invalid grammar reference\n";

            return false;
        }

        /*
         * A grammar rule can only reference
         * an older token.
         *
         * This guarantees an acyclic grammar.
         */
        const uint32_t currentToken =
            GTC_BASE_TOKENS + i;

        if (rules[i].left >= currentToken ||
            rules[i].right >= currentToken) {

            std::cerr
                << "error: forward grammar reference\n";

            return false;
        }
    }

    /*
     * GTC2:
     *
     * Two bytes per Huffman code length.
     */
    const uint64_t lengthBytes =
        static_cast<uint64_t>(
            tokenCount) * 2ULL;

    if (lengthBytes >
        data.size() - pos) {

        std::cerr
            << "error: truncated Huffman table\n";

        return false;
    }

    std::vector<uint16_t> lengths(
        tokenCount);

    for (uint32_t i = 0;
         i < tokenCount;
         ++i) {

        if (!readU16(
                data,
                pos,
                lengths[i])) {

            std::cerr
                << "error: truncated Huffman table\n";

            return false;
        }
    }

    GtcHuffman huffman;

    if (!huffman.buildFromCodeLengths(
            lengths)) {

        std::cerr
            << "error: invalid Huffman table\n";

        return false;
    }

    uint64_t bitCount;

    if (!readU64(
            data,
            pos,
            bitCount)) {

        std::cerr
            << "error: missing bit count\n";

        return false;
    }

    if (compressedSize >
        data.size() - pos) {

        std::cerr
            << "error: truncated compressed stream\n";

        return false;
    }

    /*
     * Make sure the compressed stream size
     * can safely be converted to size_t.
     */
    if (compressedSize >
        static_cast<uint64_t>(
            static_cast<size_t>(-1))) {

        std::cerr
            << "error: compressed stream too large\n";

        return false;
    }

    const size_t compressedSizeT =
        static_cast<size_t>(
            compressedSize);

    std::vector<uint8_t> compressed(
        data.begin() +
            static_cast<std::ptrdiff_t>(pos),

        data.begin() +
            static_cast<std::ptrdiff_t>(
                pos + compressedSizeT));

    if (bitCount >
        compressedSize * 8ULL) {

        std::cerr
            << "error: invalid bit count\n";

        return false;
    }

    BitReader reader(
        compressed,
        bitCount);

    std::vector<uint32_t> tokens;

    if (!huffman.decode(
            reader,
            tokenCountInStream,
            tokens)) {

        std::cerr
            << "error: invalid Huffman stream\n";

        return false;
    }

    std::vector<uint8_t> output;

    output.reserve(
        static_cast<size_t>(
            originalSize));

    GrammarExpander expander(
        rules,
        output);

    for (uint32_t token : tokens) {
        if (token >= tokenCount) {
            std::cerr
                << "error: invalid token\n";

            return false;
        }

        expander.expand(token);

        if (output.size() >
            originalSize) {

            std::cerr
                << "error: decompressed data is too large\n";

            return false;
        }
    }

    if (output.size() !=
        originalSize) {

        std::cerr
            << "error: decompressed size mismatch\n";

        return false;
    }

    if (!writeFile(
            outputName,
            output)) {

        std::cerr
            << "error: cannot write "
            << outputName
            << "\n";

        return false;
    }

    return true;
}