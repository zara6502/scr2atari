#include "gtc_table.h"
#include "gtc_decoder.h"
#include "gtc_format.h"
#include "gtc_huffman.h"

#include <cstddef>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>

namespace {


struct Rule {
    uint16_t left;
    uint16_t right;
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

            if (current < GTC_BASE_TOKENS) {
                output_.push_back(
                    static_cast<uint8_t>(
                        current));

                continue;
            }

            uint32_t index =
                current - GTC_BASE_TOKENS;

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


/*
 * Read the v5 archive index and name table.
 *
 * The stream position after this function points
 * immediately after the name table, i.e. at the
 * grammar section.
 */
bool readArchiveIndex(
    const std::vector<uint8_t>& data,
    size_t& pos,
    uint32_t fileCount,
    uint32_t indexSize,
    uint32_t namesSize,
    std::vector<GtcArchiveEntry>& entries)
{
    const uint64_t expectedIndexSize =
        static_cast<uint64_t>(fileCount) * 40ULL;

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

    std::vector<uint32_t> nameOffsets(fileCount);
    std::vector<uint32_t> nameLengths(fileCount);

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
    uint32_t& namesSize)
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
            namesSize)) {

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

    if (tokenCount !=
        GTC_BASE_TOKENS +
        grammarCount) {

        std::cerr
            << "error: invalid token count\n";

        return false;
    }

    return true;
}


bool readGrammar(
    const std::vector<uint8_t>& data,
    size_t& pos,
    uint32_t tokenCount,
    uint32_t grammarCount,
    std::vector<Rule>& rules)
{
    rules.clear();
    rules.resize(grammarCount);

    for (uint32_t i = 0;
         i < grammarCount;
         ++i) {

        if (!readU16(
                data,
                pos,
                rules[i].left) ||

            !readU16(
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
         * Grammar rule i creates token
         *
         *     GTC_BASE_TOKENS + i
         *
         * and therefore may only reference
         * tokens that already existed.
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

    std::vector<Rule> rules;

    if (!readGrammar(
            data,
            pos,
            tokenCount,
            grammarCount,
            rules)) {

        return false;
    }

    GtcHuffman huffman;

    if (!readHuffmanTable(
            data,
            pos,
            tokenCount,
            huffman)) {

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

    if (bitCount >
        compressedSize * 8ULL) {

        std::cerr
            << "error: invalid bit count\n";

        return false;
    }

    if (compressedSize >
        data.size() - pos) {

        std::cerr
            << "error: truncated compressed stream\n";

        return false;
    }

    if (!fitsSizeT(compressedSize)) {

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

    if (!fitsSizeT(originalSize)) {

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
            namesSize)) {

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

    for (const GtcArchiveEntry& entry : entries) {
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
        << "GTC archive\n"
        << "\n"
        << "files         : "
        << fileCount
        << "\n"
        << "original      : "
        << originalSize
        << " bytes\n"
        << "grammar rules : "
        << grammarCount
        << "\n"
        << "tokens        : "
        << tokenCount
        << "\n"
        << "compressed    : "
        << compressedSize
        << " bytes\n"
        << "\n";

    for (const GtcArchiveEntry& entry : entries) {
        std::cout
            << entry.originalSize
            << "\t"
            << entry.name
            << "\n";
    }

    return true;
}


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
            namesSize)) {

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

        if (entries[i].name == fileName) {
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
     * Validate the global token range.
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
     * Read the global grammar.
     */
    std::vector<Rule> rules;

    if (!readGrammar(
            data,
            pos,
            tokenCount,
            grammarCount,
            rules)) {

        return false;
    }

    /*
     * Read the global Huffman model.
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
     * Read the exact number of valid bits in
     * the global Huffman stream.
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

    if (compressedSize >
        data.size() - pos) {

        std::cerr
            << "error: truncated compressed stream\n";

        return false;
    }

    if (!fitsSizeT(compressedSize)) {

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
     * This is the important part of the v5
     * random-access design.
     *
     * The Huffman stream is global, but we can
     * jump directly to the first bit belonging
     * to the selected file.
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
     * Expand only the selected file.
     *
     * We deliberately do NOT decode the tokens
     * belonging to preceding or following files.
     */
    if (!fitsSizeT(entry.originalSize)) {

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

    for (uint32_t token : tokens) {

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