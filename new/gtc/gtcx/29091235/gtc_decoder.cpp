#include "gtc_table.h"
#include "gtc_decoder.h"
#include "gtc_format.h"
#include "gtc_huffman.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>


namespace {


// ============================================================
// Rule
// ============================================================

struct Rule {
    uint16_t left;
    uint16_t right;
};


// ============================================================
// File helpers
// ============================================================

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


// ============================================================
// Integer readers
// ============================================================

bool readU16(
    const std::vector<uint8_t>& data,
    size_t& pos,
    uint16_t& value)
{
    if (pos > data.size() ||
        data.size() - pos < 2)
        return false;

    value =
        static_cast<uint16_t>(
            data[pos]) |
        static_cast<uint16_t>(
            static_cast<uint16_t>(
                data[pos + 1]) << 8);

    pos += 2;

    return true;
}


bool readU32(
    const std::vector<uint8_t>& data,
    size_t& pos,
    uint32_t& value)
{
    if (pos > data.size() ||
        data.size() - pos < 4)
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
    if (pos > data.size() ||
        data.size() - pos < 8)
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


// ============================================================
// Size helpers
// ============================================================

bool addSize(
    uint64_t a,
    uint64_t b,
    uint64_t& result)
{
    if (b >
        std::numeric_limits<uint64_t>::max() - a)
        return false;

    result = a + b;

    return true;
}


bool fitsSizeT(
    uint64_t value)
{
    return value <=
        static_cast<uint64_t>(
            std::numeric_limits<size_t>::max());
}


bool readVarint(
    const std::vector<uint8_t>& data,
    size_t& pos,
    uint32_t& value)
{
    value = 0;

    uint32_t shift = 0;

    while (shift < 32) {

        if (pos >= data.size())
            return false;

        const uint8_t b =
            data[pos++];

        value |=
            static_cast<uint32_t>(
                b & 0x7F)
            << shift;

        if ((b & 0x80) == 0)
            return true;

        shift += 7;
    }

    return false;
}


int32_t unzigzag32(
    uint32_t value)
{
    return
        static_cast<int32_t>(
            (value >> 1) ^
            static_cast<uint32_t>(
                -static_cast<int32_t>(
                    value & 1)));
}


bool readGrammarValue(
    const std::vector<uint8_t>& data,
    size_t& pos,
    uint16_t previous,
    uint16_t& value)
{
    uint32_t encoded;

    if (!readVarint(
            data,
            pos,
            encoded)) {

        return false;
    }

    const int32_t delta =
        unzigzag32(encoded);

    const int32_t current =
        static_cast<int32_t>(previous) +
        delta;

    if (current < 0 ||
        current > UINT16_MAX) {

        return false;
    }

    value =
        static_cast<uint16_t>(current);

    return true;
}

static bool decodeGrammarValue(
    const std::vector<uint8_t>& data,
    size_t& pos,
    uint32_t previous,
    uint32_t& value)
{
    uint32_t encoded;

    if (!readVarint(
            data,
            pos,
            encoded))
        return false;

    const int32_t delta =
        unzigzag32(encoded);

    /*
     * Grammar values are uint16_t.
     *
     * Work in signed 64-bit space so that an
     * invalid delta cannot wrap around uint32_t.
     */
    const int64_t reconstructed =
        static_cast<int64_t>(
            previous) +
        static_cast<int64_t>(
            delta);

    if (reconstructed < 0 ||
        reconstructed > 65535)
        return false;

    value =
        static_cast<uint32_t>(
            reconstructed);

    return true;
}


// ============================================================
// Grammar expander
// ============================================================

class GrammarExpander {
public:

    GrammarExpander(
        const std::vector<Rule>& rules,
        std::vector<uint8_t>& output)
        : rules_(rules),
          output_(output)
    {
    }


    void expand(
        uint32_t token)
    {
        /*
         * Iterative DFS instead of recursive
         * C++ calls.
         */
        stack_.clear();

        stack_.push_back(token);

        while (!stack_.empty()) {

            uint32_t current =
                stack_.back();

            stack_.pop_back();

            if (current < GTC_BASE_TOKENS) {

                output_.push_back(
                    static_cast<uint8_t>(
                        current));

                continue;
            }

            uint32_t index =
                current -
                GTC_BASE_TOKENS;

            if (index >= rules_.size())
                return;

            const Rule& rule =
                rules_[index];

            /*
             * Reverse order because stack is LIFO.
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


// ============================================================
// OLD archive grammar reader
//
// Archive format remains unchanged:
//
//     uint16 left
//     uint16 right
//
// Do NOT replace this with compact grammar decoding.
// ============================================================

bool readGrammar(
    const std::vector<uint8_t>& data,
    size_t& pos,
    uint32_t tokenCount,
    uint32_t grammarCount,
    std::vector<Rule>& rules,
    bool compact)
{
    rules.clear();
    rules.resize(grammarCount);

    uint16_t previousLeft = 0;
    uint16_t previousRight = 0;

    for (uint32_t i = 0;
         i < grammarCount;
         ++i) {

        bool ok;

        if (compact) {

            ok =
                readGrammarValue(
                    data,
                    pos,
                    previousLeft,
                    rules[i].left) &&

                readGrammarValue(
                    data,
                    pos,
                    previousRight,
                    rules[i].right);
        }
        else {

            ok =
                readU16(
                    data,
                    pos,
                    rules[i].left) &&

                readU16(
                    data,
                    pos,
                    rules[i].right);
        }

        if (!ok) {

            std::cerr
                << "error: truncated grammar\n";

            return false;
        }

        previousLeft =
            rules[i].left;

        previousRight =
            rules[i].right;

        /*
         * Token/rule reference must be valid.
         *
         * Tokens are:
         *   0..255          base tokens
         *   256..           grammar tokens
         */
        if (rules[i].left >=
                GTC_BASE_TOKENS + grammarCount ||
            rules[i].right >=
                GTC_BASE_TOKENS + grammarCount) {

            std::cerr
                << "error: invalid grammar token\n";

            return false;
        }

        /*
         * A grammar rule may reference only:
         *   - base tokens, or
         *   - a rule which has already been decoded.
         *
         * Current rule token is 256 + i.
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

    return true;
}



// ============================================================
// Normal Huffman table reader
// ============================================================

bool readHuffmanTable(
    const std::vector<uint8_t>& data,
    size_t& pos,
    uint32_t tokenCount,
    GtcHuffman& huffman)
{
    uint32_t tableSize;

    if (!readU32(
            data,
            pos,
            tableSize)) {

        std::cerr
            << "error: missing Huffman table size\n";

        return false;
    }

    if (tableSize >
        data.size() - pos) {

        std::cerr
            << "error: truncated Huffman table\n";

        return false;
    }

    std::vector<uint8_t> tableData(
        data.begin() +
            static_cast<std::ptrdiff_t>(pos),

        data.begin() +
            static_cast<std::ptrdiff_t>(
                pos + tableSize));

    pos += tableSize;

    std::vector<uint16_t> lengths;

    GtcLengthTable table;

    if (!table.decode(
            tableData,
            tokenCount,
            lengths)) {

        std::cerr
            << "error: invalid Huffman table\n";

        return false;
    }

    if (!huffman.buildFromCodeLengths(
            lengths)) {

        std::cerr
            << "error: invalid Huffman code lengths\n";

        return false;
    }

    return true;
}


// ============================================================
// Archive Huffman tables
// ============================================================

bool readHuffmanTables(
    const std::vector<uint8_t>& data,
    size_t& pos,
    uint32_t tokenCount,
    uint32_t modelCount,
    std::vector<GtcHuffman>& huffmans)
{
    huffmans.clear();
    huffmans.resize(modelCount);

    for (uint32_t modelIndex = 0;
         modelIndex < modelCount;
         ++modelIndex) {

        uint32_t tableSize;

        if (!readU32(
                data,
                pos,
                tableSize)) {

            std::cerr
                << "error: missing Huffman table size\n";

            return false;
        }

        if (tableSize >
            data.size() - pos) {

            std::cerr
                << "error: truncated Huffman table\n";

            return false;
        }

        std::vector<uint8_t> tableData(
            data.begin() +
                static_cast<std::ptrdiff_t>(pos),

            data.begin() +
                static_cast<std::ptrdiff_t>(
                    pos + tableSize));

        pos += tableSize;

        std::vector<uint16_t> lengths;

        GtcLengthTable table;

        if (!table.decode(
                tableData,
                tokenCount,
                lengths)) {

            std::cerr
                << "error: invalid Huffman table\n";

            return false;
        }

        if (!huffmans[modelIndex].buildFromCodeLengths(
                lengths)) {

            std::cerr
                << "error: invalid Huffman code lengths\n";

            return false;
        }
    }

    return true;
}


// ============================================================
// Archive index
// ============================================================

bool readArchiveIndex(
    const std::vector<uint8_t>& data,
    size_t& pos,
    uint32_t fileCount,
    uint32_t indexSize,
    uint32_t namesSize,
    std::vector<GtcArchiveEntry>& entries)
{
    const uint64_t expectedIndexSize =
        static_cast<uint64_t>(fileCount) *
        40ULL;

    if (expectedIndexSize != indexSize) {

        std::cerr
            << "error: invalid archive index size\n";

        return false;
    }

    if (indexSize >
        data.size() - pos) {

        std::cerr
            << "error: truncated archive index\n";

        return false;
    }

    const size_t indexBegin = pos;

    entries.clear();
    entries.resize(fileCount);

    std::vector<uint32_t> nameOffsets(
        fileCount);

    std::vector<uint32_t> nameLengths(
        fileCount);

    for (uint32_t i = 0;
         i < fileCount;
         ++i) {

        if (!readU32(
                data,
                pos,
                nameOffsets[i]) ||

            !readU32(
                data,
                pos,
                nameLengths[i]) ||

            !readU64(
                data,
                pos,
                entries[i].originalSize) ||

            !readU64(
                data,
                pos,
                entries[i].tokenStart) ||

            !readU64(
                data,
                pos,
                entries[i].tokenCount) ||

            !readU64(
                data,
                pos,
                entries[i].bitOffset)) {

            std::cerr
                << "error: truncated archive index\n";

            return false;
        }
    }

    if (pos - indexBegin != indexSize) {

        std::cerr
            << "error: invalid archive index\n";

        return false;
    }

    if (namesSize >
        data.size() - pos) {

        std::cerr
            << "error: truncated archive names\n";

        return false;
    }

    const size_t namesBegin = pos;

    for (uint32_t i = 0;
         i < fileCount;
         ++i) {

        const uint64_t nameEnd =
            static_cast<uint64_t>(
                nameOffsets[i]) +
            static_cast<uint64_t>(
                nameLengths[i]);

        if (nameEnd > namesSize) {

            std::cerr
                << "error: invalid archive name range\n";

            return false;
        }

        const size_t begin =
            namesBegin +
            static_cast<size_t>(
                nameOffsets[i]);

        entries[i].name.assign(
            reinterpret_cast<const char*>(
                data.data() + begin),
            static_cast<size_t>(
                nameLengths[i]));
    }

    pos += namesSize;

    return true;
}


// ============================================================
// Archive header
// ============================================================

bool readArchiveHeader(
    const std::vector<uint8_t>& data,
    size_t& pos,
    uint64_t& originalSize,
    uint32_t& tokenCount,
    uint32_t& grammarCount,
    uint64_t& tokenCountInStream,
    uint64_t& compressedSize,
    uint32_t& fileCount,
    uint32_t& indexSize,
    uint32_t& namesSize,
    uint32_t& huffmanModelCount)
{
    uint32_t magic;
    uint32_t version;

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
            compressedSize) ||

        !readU32(
            data,
            pos,
            fileCount) ||

        !readU32(
            data,
            pos,
            indexSize) ||

        !readU32(
            data,
            pos,
            namesSize) ||

        !readU32(
            data,
            pos,
            huffmanModelCount)) {

        std::cerr
            << "error: truncated archive header\n";

        return false;
    }

    if (magic != GTC_MAGIC) {

        std::cerr
            << "error: invalid GTC archive\n";

        return false;
    }

    if (version != GTC_ARCHIVE_VERSION) {

        std::cerr
            << "error: unsupported GTC archive version\n";

        return false;
    }

    if (huffmanModelCount == 0) {

        std::cerr
            << "error: invalid Huffman model count\n";

        return false;
    }

    if (huffmanModelCount != 1 &&
        huffmanModelCount != fileCount) {

        std::cerr
            << "error: invalid Huffman model count\n";

        return false;
    }

    if (tokenCount !=
        GTC_BASE_TOKENS +
        grammarCount) {

        std::cerr
            << "error: invalid token count\n";

        return false;
    }

    return true;
}


} // namespace


// ============================================================
// GtcDecoder public integer helpers
// ============================================================

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


// ============================================================
// Decode normal GTC X file
//
// IMPORTANT:
// This now expects the NEW compact grammar format.
//
//     delta + zigzag + varint
//
// It is intentionally separate from archive grammar
// decoding, because archives still use their own format.
// ============================================================

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

    /*
     * --------------------------------------------------------
     * Header
     * --------------------------------------------------------
     */
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

    /*
     * --------------------------------------------------------
     * NEW COMPACT GRAMMAR
     * --------------------------------------------------------
     */
    std::vector<Rule> rules;

if (!readGrammar(
        data,
        pos,
        tokenCount,
        grammarCount,
        rules,
        true)) {

        return false;
    }

    /*
     * --------------------------------------------------------
     * Huffman table
     * --------------------------------------------------------
     */
    GtcHuffman huffman;

    if (!readHuffmanTable(
            data,
            pos,
            tokenCount,
            huffman)) {

        return false;
    }

    /*
     * --------------------------------------------------------
     * Valid bit count
     * --------------------------------------------------------
     */
    uint64_t bitCount;

    if (!readU64(
            data,
            pos,
            bitCount)) {

        std::cerr
            << "error: missing bit count\n";

        return false;
    }

    if (bitCount >
        compressedSize * 8ULL) {

        std::cerr
            << "error: invalid bit count\n";

        return false;
    }

    /*
     * --------------------------------------------------------
     * Huffman stream
     * --------------------------------------------------------
     */
    if (compressedSize >
        data.size() - pos) {

        std::cerr
            << "error: truncated compressed stream\n";

        return false;
    }

    if (!fitsSizeT(
            compressedSize)) {

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

    /*
     * There should be no reason for arbitrary data
     * after the compressed stream. We don't require
     * pos == data.size() here because the existing
     * format did not require it either.
     */
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

    /*
     * --------------------------------------------------------
     * Grammar expansion
     * --------------------------------------------------------
     */
    if (!fitsSizeT(
            originalSize)) {

        std::cerr
            << "error: decompressed file too large\n";

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

        const size_t oldSize =
            output.size();

        expander.expand(token);

        /*
         * GrammarExpander historically has a void
         * interface. If an invalid rule somehow
         * gets through, the size checks below still
         * protect the output.
         */
        if (output.size() <
            oldSize) {

            std::cerr
                << "error: decompression size overflow\n";

            return false;
        }

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

    /*
     * --------------------------------------------------------
     * Output
     * --------------------------------------------------------
     */
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


// ============================================================
// List archive
//
// Archive grammar remains the old uint16/uint16 format.
// ============================================================

bool GtcDecoder::listArchive(
    const std::string& archiveName)
{
    std::vector<uint8_t> data;

    if (!readFile(
            archiveName,
            data)) {

        std::cerr
            << "error: cannot read "
            << archiveName
            << "\n";

        return false;
    }

    size_t pos = 0;

    uint64_t originalSize;
    uint32_t tokenCount;
    uint32_t grammarCount;
    uint64_t tokenCountInStream;
    uint64_t compressedSize;
    uint32_t fileCount;
    uint32_t indexSize;
    uint32_t namesSize;
    uint32_t huffmanModelCount = 0;

    if (!readArchiveHeader(
            data,
            pos,
            originalSize,
            tokenCount,
            grammarCount,
            tokenCountInStream,
            compressedSize,
            fileCount,
            indexSize,
            namesSize,
            huffmanModelCount)) {

        return false;
    }

    std::vector<GtcArchiveEntry> entries;

    if (!readArchiveIndex(
            data,
            pos,
            fileCount,
            indexSize,
            namesSize,
            entries)) {

        return false;
    }

    uint64_t totalSize = 0;

    for (const GtcArchiveEntry& entry :
         entries) {

        if (!addSize(
                totalSize,
                entry.originalSize,
                totalSize)) {

            std::cerr
                << "error: archive size overflow\n";

            return false;
        }
    }

    if (totalSize != originalSize) {

        std::cerr
            << "error: archive original size mismatch\n";

        return false;
    }

    std::cout
        << "files         : "
        << fileCount
        << "\n"
        << "original      : "
        << originalSize
        << " bytes\n"
        << "tokens        : "
        << tokenCountInStream
        << "\n"
        << "grammar       : "
        << grammarCount
        << "\n"
        << "huffman models: "
        << huffmanModelCount
        << "\n"
        << "compressed    : "
        << compressedSize
        << " bytes\n";

    for (const GtcArchiveEntry& entry :
         entries) {

        std::cout
            << entry.originalSize
            << "\t"
            << entry.name
            << "\n";
    }

    return true;
}


// ============================================================
// Extract archive file
//
// Archive grammar remains the old uint16/uint16 format.
// ============================================================

bool GtcDecoder::extractArchiveFile(
    const std::string& archiveName,
    const std::string& fileName,
    const std::string& outputName)
{
    std::vector<uint8_t> data;

    if (!readFile(
            archiveName,
            data)) {

        std::cerr
            << "error: cannot read "
            << archiveName
            << "\n";

        return false;
    }

    size_t pos = 0;

    uint64_t archiveOriginalSize;
    uint32_t tokenCount;
    uint32_t grammarCount;
    uint64_t tokenCountInStream;
    uint64_t compressedSize;
    uint32_t fileCount;
    uint32_t indexSize;
    uint32_t namesSize;
    uint32_t huffmanModelCount = 0;

    if (!readArchiveHeader(
            data,
            pos,
            archiveOriginalSize,
            tokenCount,
            grammarCount,
            tokenCountInStream,
            compressedSize,
            fileCount,
            indexSize,
            namesSize,
            huffmanModelCount)) {

        return false;
    }

    std::vector<GtcArchiveEntry> entries;

    if (!readArchiveIndex(
            data,
            pos,
            fileCount,
            indexSize,
            namesSize,
            entries)) {

        return false;
    }

    size_t selectedIndex =
        static_cast<size_t>(-1);

    for (size_t i = 0;
         i < entries.size();
         ++i) {

        if (entries[i].name ==
            fileName) {

            selectedIndex = i;
            break;
        }
    }

    if (selectedIndex ==
        static_cast<size_t>(-1)) {

        std::cerr
            << "error: file not found in archive: "
            << fileName
            << "\n";

        return false;
    }

    const GtcArchiveEntry& entry =
        entries[selectedIndex];

    /*
     * --------------------------------------------------------
     * Validate global token range.
     * --------------------------------------------------------
     */
    if (entry.tokenStart >
        tokenCountInStream) {

        std::cerr
            << "error: invalid token start\n";

        return false;
    }

    if (entry.tokenCount >
        tokenCountInStream -
        entry.tokenStart) {

        std::cerr
            << "error: invalid token count in archive entry\n";

        return false;
    }

    /*
     * --------------------------------------------------------
     * Read archive grammar.
     *
     * IMPORTANT:
     * archive format is unchanged.
     * --------------------------------------------------------
     */
    std::vector<Rule> rules;

if (!readGrammar(
        data,
        pos,
        tokenCount,
        grammarCount,
        rules,
        false)) {

        return false;
    }

    /*
     * --------------------------------------------------------
     * Read all Huffman models.
     * --------------------------------------------------------
     */
    std::vector<GtcHuffman> huffmans;

    if (!readHuffmanTables(
            data,
            pos,
            tokenCount,
            huffmanModelCount,
            huffmans)) {

        return false;
    }

    const GtcHuffman& huffman =
        huffmanModelCount == 1
            ? huffmans[0]
            : huffmans[selectedIndex];

    /*
     * --------------------------------------------------------
     * Valid global bit count.
     * --------------------------------------------------------
     */
    uint64_t bitCount;

    if (!readU64(
            data,
            pos,
            bitCount)) {

        std::cerr
            << "error: missing bit count\n";

        return false;
    }

    if (bitCount >
        compressedSize * 8ULL) {

        std::cerr
            << "error: invalid bit count\n";

        return false;
    }

    if (entry.bitOffset >
        bitCount) {

        std::cerr
            << "error: invalid archive bit offset\n";

        return false;
    }

    /*
     * --------------------------------------------------------
     * Compressed stream.
     * --------------------------------------------------------
     */
    if (compressedSize >
        data.size() - pos) {

        std::cerr
            << "error: truncated compressed stream\n";

        return false;
    }

    if (!fitsSizeT(
            compressedSize)) {

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

    /*
     * --------------------------------------------------------
     * Random-access decode.
     * --------------------------------------------------------
     */
    BitReader reader(
        compressed,
        bitCount);

    if (!reader.seek(
            entry.bitOffset)) {

        std::cerr
            << "error: cannot seek to archive entry\n";

        return false;
    }

    std::vector<uint32_t> tokens;

    if (!huffman.decode(
            reader,
            entry.tokenCount,
            tokens)) {

        std::cerr
            << "error: invalid Huffman stream for archive entry\n";

        return false;
    }

    /*
     * --------------------------------------------------------
     * Expand selected file.
     * --------------------------------------------------------
     */
    if (!fitsSizeT(
            entry.originalSize)) {

        std::cerr
            << "error: extracted file too large\n";

        return false;
    }

    std::vector<uint8_t> output;

    output.reserve(
        static_cast<size_t>(
            entry.originalSize));

    GrammarExpander expander(
        rules,
        output);

    for (uint32_t token :
         tokens) {

        if (token >= tokenCount) {

            std::cerr
                << "error: invalid token in archive entry\n";

            return false;
        }

        expander.expand(token);

        if (output.size() >
            entry.originalSize) {

            std::cerr
                << "error: extracted data is too large\n";

            return false;
        }
    }

    if (output.size() !=
        entry.originalSize) {

        std::cerr
            << "error: extracted size mismatch\n";

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