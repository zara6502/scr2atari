#include "gtc_table.h"
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

void GtcEncoder::buildGrammarMultiFile(
    const std::vector<std::vector<uint8_t>>& files,
    GtcDictionary& dictionary,
    std::vector<uint32_t>& sequence)
{
    sequence.clear();

    size_t totalSize = 0;

    for (const auto& file : files)
        totalSize += file.size();

    sequence.reserve(
        totalSize + files.size());

    /*
     * The special boundary token is only an internal
     * marker. It prevents grammar rules from crossing
     * from one file into another.
     */
    for (size_t fileIndex = 0;
         fileIndex < files.size();
         ++fileIndex) {

        for (uint8_t c : files[fileIndex])
            sequence.push_back(c);

        /*
         * Boundary after every file, including the last.
         */
        sequence.push_back(
            GTC_FILE_BOUNDARY);
    }

    while (sequence.size() >= 2) {
        std::unordered_map<
            uint64_t,
            PairInfo> pairs;

        pairs.reserve(
            sequence.size());

        for (size_t i = 0;
             i + 1 < sequence.size();
             ++i) {

            const uint32_t a =
                sequence[i];

            const uint32_t b =
                sequence[i + 1];

            /*
             * Never allow a grammar rule to contain
             * the internal file boundary.
             */
            if (a == GTC_FILE_BOUNDARY ||
                b == GTC_FILE_BOUNDARY)
                continue;

            const uint64_t key =
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

        if (best.count < 3)
            break;

        const uint32_t newToken =
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
                sequence[i] != GTC_FILE_BOUNDARY &&
                sequence[i + 1] != GTC_FILE_BOUNDARY &&
                sequence[i] == best.left &&
                sequence[i + 1] == best.right) {

                next.push_back(
                    newToken);

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

GtcLengthTable table;

std::vector<uint8_t> tableData;
GtcTableStats tableStats;

if (!table.encode(
        huffman.codeLengths(),
        tableData,
        tableStats)) {
    return false;
}
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
 * Compact Huffman code-length table.
 */

writeU32(
    out,
    static_cast<uint32_t>(
        tableData.size()));

if (!tableData.empty()) {
    out.write(
        reinterpret_cast<const char*>(
            tableData.data()),
        static_cast<std::streamsize>(
            tableData.size()));
}

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

bool GtcEncoder::writeArchive(
    const std::string& filename,
    const std::vector<GtcArchiveFile>& files,
    const GtcDictionary& dictionary,
    const std::vector<uint32_t>& sequence,
    GtcArchiveStats& stats)
{
    const uint32_t tokenCount =
        GTC_BASE_TOKENS +
        dictionary.size();

    /*
     * The sequence still contains internal file
     * boundary markers. Remove them while creating
     * the final token stream.
     */
    std::vector<uint32_t> tokens;

    tokens.reserve(
        sequence.size());

    for (uint32_t token : sequence) {
        if (token == GTC_FILE_BOUNDARY)
            continue;

        if (token >= tokenCount)
            return false;

        tokens.push_back(token);
    }

    std::vector<uint64_t> frequencies(
        tokenCount,
        0);

    for (uint32_t token : tokens)
        ++frequencies[token];

    GtcHuffman huffman;

    if (!huffman.build(frequencies))
        return false;

    /*
     * We need exact bit offsets for every file.
     *
     * Therefore encode each file's token range
     * separately into the same BitWriter.
     */
    BitWriter writer;

    std::vector<GtcArchiveFile> index =
        files;

    size_t tokenPosition = 0;

    for (size_t fileIndex = 0;
         fileIndex < index.size();
         ++fileIndex) {

        GtcArchiveFile& entry =
            index[fileIndex];

        entry.tokenStart =
            tokenPosition;

        entry.bitOffset =
            writer.bitCount();

        for (uint64_t i = 0;
             i < entry.tokenCount;
             ++i) {

            if (tokenPosition >=
                tokens.size())
                return false;

            const uint32_t token =
                tokens[tokenPosition++];

            std::vector<uint32_t> oneToken;
            oneToken.push_back(token);

            huffman.encode(
                oneToken,
                writer);
        }
    }

    if (tokenPosition != tokens.size())
        return false;

    writer.flush();

    const std::vector<uint8_t>& compressed =
        writer.data();

    /*
     * Compact Huffman table.
     */
    GtcLengthTable table;

    std::vector<uint8_t> tableData;

    GtcTableStats tableStats;

    if (!table.encode(
            huffman.codeLengths(),
            tableData,
            tableStats)) {
        return false;
    }

    /*
     * Build the filename table.
     */
    std::vector<uint8_t> names;

    for (const GtcArchiveFile& entry : index) {
        names.insert(
            names.end(),
            entry.name.begin(),
            entry.name.end());

        names.push_back(0);
    }

    /*
     * Archive index entry:
     *
     * nameOffset   u32
     * nameLength   u32
     * originalSize u64
     * tokenStart   u64
     * tokenCount   u64
     * bitOffset    u64
     *
     * 40 bytes per file.
     */
    const uint32_t indexEntrySize = 40;

    const uint64_t indexSize64 =
        static_cast<uint64_t>(
            index.size()) *
        indexEntrySize;

    if (indexSize64 >
        UINT32_MAX ||
        names.size() >
        UINT32_MAX) {
        return false;
    }

    std::ofstream out(
        filename,
        std::ios::binary);

    if (!out)
        return false;

    GtcArchiveHeader header{};

    header.magic =
        GTC_MAGIC;

    header.version =
        GTC_ARCHIVE_VERSION;

    for (const GtcArchiveFile& entry : index)
        header.original_size +=
            entry.originalSize;

    header.token_count =
        tokenCount;

    header.grammar_count =
        dictionary.size();

    header.token_count_in_stream =
        tokens.size();

    header.compressed_size =
        compressed.size();

    header.file_count =
        static_cast<uint32_t>(
            index.size());

    header.index_size =
        static_cast<uint32_t>(
            indexSize64);

    header.names_size =
        static_cast<uint32_t>(
            names.size());

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

    writeU32(
        out,
        header.file_count);

    writeU32(
        out,
        header.index_size);

    writeU32(
        out,
        header.names_size);

    /*
     * Index.
     */
    uint32_t nameOffset = 0;

    for (const GtcArchiveFile& entry : index) {
        writeU32(
            out,
            nameOffset);

        writeU32(
            out,
            static_cast<uint32_t>(
                entry.name.size()));

        writeU64(
            out,
            entry.originalSize);

        writeU64(
            out,
            entry.tokenStart);

        writeU64(
            out,
            entry.tokenCount);

        writeU64(
            out,
            entry.bitOffset);

        nameOffset +=
            static_cast<uint32_t>(
                entry.name.size() + 1);
    }

    /*
     * Names.
     */
    if (!names.empty()) {
        out.write(
            reinterpret_cast<const char*>(
                names.data()),
            static_cast<std::streamsize>(
                names.size()));
    }

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
     */
    writeU32(
        out,
        static_cast<uint32_t>(
            tableData.size()));

    if (!tableData.empty()) {
        out.write(
            reinterpret_cast<const char*>(
                tableData.data()),
            static_cast<std::streamsize>(
                tableData.size()));
    }

    /*
     * Valid bit count.
     */
    writeU64(
        out,
        writer.bitCount());

    /*
     * Compressed stream.
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
        header.original_size;

    stats.finalTokenCount =
        tokens.size();

    stats.tokenCount =
        tokenCount;

    stats.grammarCount =
        dictionary.size();

    stats.compressedBits =
        writer.bitCount();

    stats.compressedBytes =
        compressed.size();

    stats.fileCount =
        static_cast<uint32_t>(
            index.size());

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
bool GtcEncoder::encodeArchive(
    const std::vector<std::string>& inputNames,
    const std::string& outputName,
    GtcArchiveStats& stats)
{
    if (inputNames.empty()) {
        std::fprintf(
            stderr,
            "error: no input files\n");

        return false;
    }

    std::vector<
        std::vector<uint8_t>> inputs;

    inputs.reserve(
        inputNames.size());

    std::vector<GtcArchiveFile> files;

    files.reserve(
        inputNames.size());

    for (const std::string& filename :
         inputNames) {

        std::vector<uint8_t> data;

        if (!readFile(
                filename,
                data)) {

            std::fprintf(
                stderr,
                "error: cannot read %s\n",
                filename.c_str());

            return false;
        }

        GtcArchiveFile entry;

        entry.name =
            filename;

        entry.originalSize =
            data.size();

        inputs.push_back(
            std::move(data));

        files.push_back(
            std::move(entry));
    }

    GtcDictionary dictionary;

    std::vector<uint32_t> sequence;

    buildGrammarMultiFile(
        inputs,
        dictionary,
        sequence);

    /*
     * Determine final token counts for every file.
     *
     * The grammar sequence still contains the internal
     * boundary markers.
     */
    size_t sequencePosition = 0;

    for (size_t fileIndex = 0;
         fileIndex < inputs.size();
         ++fileIndex) {

        uint64_t count = 0;

        while (sequencePosition <
                   sequence.size() &&
               sequence[sequencePosition] !=
                   GTC_FILE_BOUNDARY) {

            ++count;
            ++sequencePosition;
        }

        files[fileIndex].tokenCount =
            count;

        /*
         * Skip the internal boundary.
         */
        if (sequencePosition <
            sequence.size() &&
            sequence[sequencePosition] ==
                GTC_FILE_BOUNDARY) {

            ++sequencePosition;
        }
    }

    if (sequencePosition !=
        sequence.size()) {

        std::fprintf(
            stderr,
            "error: invalid file boundaries\n");

        return false;
    }

    if (!writeArchive(
            outputName,
            files,
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