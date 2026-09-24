#include "gtc_table.h"
#include "gtc_encoder.h"
#include "gtc_format.h"
#include "gtc_huffman.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <unordered_map>
#include <limits>


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


bool addArchiveSize(
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


/*
 * Write the common archive prefix:
 *
 *     header
 *     index
 *     names
 *     grammar
 *
 * The Huffman-specific part is written by the
 * selected model below.
 */
bool writeArchiveCommon(
    std::ofstream& out,
    const std::vector<GtcArchiveFile>& index,
    const std::vector<uint8_t>& names,
    const GtcDictionary& dictionary)
{
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

    if (!names.empty()) {
        out.write(
            reinterpret_cast<const char*>(
                names.data()),
            static_cast<std::streamsize>(
                names.size()));
    }

    /*
     * Grammar.
     *
     * Exactly 4 bytes per rule:
     *
     *     uint16 left
     *     uint16 right
     */
    for (uint32_t i = 0;
         i < dictionary.size();
         ++i) {

        const GtcRule& rule =
            dictionary.rule(
                GTC_BASE_TOKENS + i);

        writeU16(
            out,
            rule.left);

        writeU16(
            out,
            rule.right);
    }

    return static_cast<bool>(out);
}


/*
 * Calculate the fixed part common to v6 and v7.
 */
uint64_t archiveFixedSize(
    uint64_t headerSize,
    uint64_t indexSize,
    uint64_t namesSize,
    uint64_t grammarCount)
{
    uint64_t size = 0;

    if (!addArchiveSize(
            size,
            headerSize,
            size))
        return UINT64_MAX;

    if (!addArchiveSize(
            size,
            indexSize,
            size))
        return UINT64_MAX;

    if (!addArchiveSize(
            size,
            namesSize,
            size))
        return UINT64_MAX;

    if (!addArchiveSize(
            size,
            grammarCount * 4ULL,
            size))
        return UINT64_MAX;

    return size;
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
            tableStats))
        return false;

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

    for (uint32_t i = 0;
         i < dictionary.size();
         ++i) {

        const GtcRule& rule =
            dictionary.rule(
                GTC_BASE_TOKENS + i);

        writeU16(
            out,
            rule.left);

        writeU16(
            out,
            rule.right);
    }

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

    writeU64(
        out,
        writer.bitCount());

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

    if (files.empty())
        return false;

    /*
     * Remove internal file-boundary markers.
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

    /*
     * Build filename table.
     */
    std::vector<uint8_t> names;

    for (const GtcArchiveFile& entry : files) {

        names.insert(
            names.end(),
            entry.name.begin(),
            entry.name.end());

        names.push_back(0);
    }

    const uint32_t indexEntrySize = 40;

    const uint64_t indexSize64 =
        static_cast<uint64_t>(
            files.size()) *
        indexEntrySize;

    if (indexSize64 > UINT32_MAX ||
        names.size() > UINT32_MAX)
        return false;

    /*
     * --------------------------------------------------------
     * Candidate A: global Huffman, archive v6
     * --------------------------------------------------------
     */
    GtcHuffman globalHuffman;

    std::vector<uint64_t> globalFrequencies(
        tokenCount,
        0);

    for (uint32_t token : tokens)
        ++globalFrequencies[token];

    if (!globalHuffman.build(
            globalFrequencies))
        return false;

    BitWriter globalWriter;

    std::vector<GtcArchiveFile> globalIndex =
        files;

    size_t tokenPosition = 0;

    for (size_t fileIndex = 0;
         fileIndex < globalIndex.size();
         ++fileIndex) {

        GtcArchiveFile& entry =
            globalIndex[fileIndex];

        entry.tokenStart =
            tokenPosition;

        entry.bitOffset =
            globalWriter.bitCount();

        if (entry.tokenCount >
            tokens.size() - tokenPosition)
            return false;

        const size_t count =
            static_cast<size_t>(
                entry.tokenCount);

        std::vector<uint32_t> fileTokens;

        fileTokens.reserve(count);

        for (size_t i = 0;
             i < count;
             ++i) {

            fileTokens.push_back(
                tokens[tokenPosition + i]);
        }

        globalHuffman.encode(
            fileTokens,
            globalWriter);

        tokenPosition += count;
    }

    if (tokenPosition != tokens.size())
        return false;

    globalWriter.flush();

    GtcLengthTable lengthTable;

    std::vector<uint8_t> globalTableData;
    GtcTableStats globalTableStats;

    if (!lengthTable.encode(
            globalHuffman.codeLengths(),
            globalTableData,
            globalTableStats))
        return false;

    /*
     * Exact v6 archive size.
     *
     * Header = 56 bytes.
     */
    const uint64_t globalFixed =
        archiveFixedSize(
            56,
            indexSize64,
            names.size(),
            dictionary.size());

    if (globalFixed == UINT64_MAX)
        return false;

    uint64_t globalArchiveSize =
        globalFixed;

    if (!addArchiveSize(
            globalArchiveSize,
            4ULL +
                globalTableData.size(),
            globalArchiveSize))
        return false;

    if (!addArchiveSize(
            globalArchiveSize,
            8ULL,
            globalArchiveSize))
        return false;

    if (!addArchiveSize(
            globalArchiveSize,
            globalWriter.data().size(),
            globalArchiveSize))
        return false;

    /*
     * --------------------------------------------------------
     * Candidate B: compact per-file Huffman, archive v7
     * --------------------------------------------------------
     *
     * v7 stores:
     *
     *     global code lengths
     *     delta alphabet
     *     delta Huffman table
     *     delta stream
     *
     * The main token stream is still one continuous stream
     * and the existing 40-byte index keeps exact bit offsets.
     */
    bool deltaCandidateValid = true;

    std::vector<GtcHuffman> fileHuffmans;

    BitWriter deltaMainWriter;

    std::vector<GtcArchiveFile> deltaIndex =
        files;

    /*
     * An empty file cannot have an independent Huffman
     * model. In that case simply disable the delta candidate.
     */
    for (const GtcArchiveFile& entry : files) {

        if (entry.tokenCount == 0) {
            deltaCandidateValid = false;
            break;
        }
    }

    if (deltaCandidateValid) {

        fileHuffmans.resize(
            files.size());

        tokenPosition = 0;

        for (size_t fileIndex = 0;
             fileIndex < files.size();
             ++fileIndex) {

            const GtcArchiveFile& entry =
                files[fileIndex];

            std::vector<uint64_t> frequencies(
                tokenCount,
                0);

            if (entry.tokenCount >
                tokens.size() - tokenPosition) {

                deltaCandidateValid = false;
                break;
            }

            for (uint64_t i = 0;
                 i < entry.tokenCount;
                 ++i) {

                ++frequencies[
                    tokens[
                        tokenPosition +
                        static_cast<size_t>(i)]];
            }

            if (!fileHuffmans[fileIndex].build(
                    frequencies)) {

                deltaCandidateValid = false;
                break;
            }

            tokenPosition +=
                static_cast<size_t>(
                    entry.tokenCount);
        }

        if (tokenPosition != tokens.size())
            deltaCandidateValid = false;
    }

    std::vector<int8_t> deltaValues;

    std::vector<uint8_t> deltaSymbols;

    GtcHuffman deltaHuffman;

    std::vector<uint8_t> deltaTableData;

    BitWriter deltaWriter;

    if (deltaCandidateValid) {

        /*
         * Map every distinct signed code-length delta
         * to a compact Huffman symbol.
         *
         * The measured project data has very small deltas
         * (maximum absolute value 13).
         *
         * We deliberately use int8 here. If a pathological
         * input produces a delta outside [-128,127], this
         * candidate is simply rejected and global Huffman
         * remains available.
         */
        std::map<int, uint32_t> deltaMap;

        for (size_t fileIndex = 0;
             fileIndex < fileHuffmans.size();
             ++fileIndex) {

            const std::vector<uint16_t>&
                globalLengths =
                    globalHuffman.codeLengths();

            const std::vector<uint16_t>&
                fileLengths =
                    fileHuffmans[fileIndex].codeLengths();

            if (globalLengths.size() !=
                    tokenCount ||
                fileLengths.size() !=
                    tokenCount) {

                deltaCandidateValid = false;
                break;
            }

            for (uint32_t symbol = 0;
                 symbol < tokenCount;
                 ++symbol) {

                const int delta =
                    static_cast<int>(
                        fileLengths[symbol]) -
                    static_cast<int>(
                        globalLengths[symbol]);

                if (delta < -128 ||
                    delta > 127) {

                    deltaCandidateValid = false;
                    break;
                }

                if (deltaMap.find(delta) ==
                    deltaMap.end()) {

                    const uint32_t index =
                        static_cast<uint32_t>(
                            deltaValues.size());

                    deltaMap.emplace(
                        delta,
                        index);

                    deltaValues.push_back(
                        static_cast<int8_t>(
                            delta));
                }
            }

            if (!deltaCandidateValid)
                break;
        }

        if (deltaValues.empty())
            deltaCandidateValid = false;

        if (deltaValues.size() > 256)
            deltaCandidateValid = false;

        /*
         * Convert the per-file code-length arrays to the
         * compact delta alphabet.
         *
         * Layout is:
         *
         *     file 0: token 0..tokenCount-1
         *     file 1: token 0..tokenCount-1
         *     ...
         */
        if (deltaCandidateValid) {

            deltaSymbols.reserve(
                files.size() *
                static_cast<size_t>(
                    tokenCount));

            for (size_t fileIndex = 0;
                 fileIndex < fileHuffmans.size();
                 ++fileIndex) {

                const std::vector<uint16_t>&
                    globalLengths =
                        globalHuffman.codeLengths();

                const std::vector<uint16_t>&
                    fileLengths =
                        fileHuffmans[fileIndex].codeLengths();

                for (uint32_t symbol = 0;
                     symbol < tokenCount;
                     ++symbol) {

                    const int delta =
                        static_cast<int>(
                            fileLengths[symbol]) -
                        static_cast<int>(
                            globalLengths[symbol]);

                    auto it =
                        deltaMap.find(delta);

                    if (it == deltaMap.end()) {
                        deltaCandidateValid = false;
                        break;
                    }

                    deltaSymbols.push_back(
                        static_cast<uint8_t>(
                            it->second));
                }

                if (!deltaCandidateValid)
                    break;
            }
        }
    }

    if (deltaCandidateValid) {

        std::vector<uint64_t>
            deltaFrequencies(
                deltaValues.size(),
                0);

        for (uint8_t symbol :
             deltaSymbols) {

            if (symbol >=
                deltaFrequencies.size()) {

                deltaCandidateValid = false;
                break;
            }

            ++deltaFrequencies[symbol];
        }

        if (deltaCandidateValid) {

            if (!deltaHuffman.build(
                    deltaFrequencies)) {

                deltaCandidateValid = false;
            }
        }
    }

    if (deltaCandidateValid) {

        /*
         * Encode the compact delta stream.
         */
std::vector<uint32_t> deltaHuffmanSymbols;
deltaHuffmanSymbols.reserve(deltaSymbols.size());

for (uint8_t symbol : deltaSymbols) {
    deltaHuffmanSymbols.push_back(static_cast<uint32_t>(symbol));
}

deltaHuffman.encode(
    deltaHuffmanSymbols,
    deltaWriter);

        deltaWriter.flush();

        GtcTableStats deltaTableStats;

        if (!lengthTable.encode(
                deltaHuffman.codeLengths(),
                deltaTableData,
                deltaTableStats)) {

            deltaCandidateValid = false;
        }
    }

    if (deltaCandidateValid) {

        /*
         * Encode the main token stream with the selected
         * Huffman model of each file.
         */
        tokenPosition = 0;

        for (size_t fileIndex = 0;
             fileIndex < files.size();
             ++fileIndex) {

            GtcArchiveFile& entry =
                deltaIndex[fileIndex];

            entry.tokenStart =
                tokenPosition;

            entry.bitOffset =
                deltaMainWriter.bitCount();

            if (entry.tokenCount >
                tokens.size() - tokenPosition) {

                deltaCandidateValid = false;
                break;
            }

            const size_t count =
                static_cast<size_t>(
                    entry.tokenCount);

            std::vector<uint32_t> fileTokens;

            fileTokens.reserve(count);

            for (size_t i = 0;
                 i < count;
                 ++i) {

                fileTokens.push_back(
                    tokens[tokenPosition + i]);
            }

            fileHuffmans[fileIndex].encode(
                fileTokens,
                deltaMainWriter);

            tokenPosition += count;
        }

        if (tokenPosition != tokens.size())
            deltaCandidateValid = false;
    }

    if (deltaCandidateValid)
        deltaMainWriter.flush();

    uint64_t deltaArchiveSize =
        UINT64_MAX;

    if (deltaCandidateValid) {

        /*
         * v7 header = 60 bytes.
         *
         * After grammar:
         *
         *     u32 global table size
         *     global table
         *
         *     u16 delta alphabet count
         *     int8 delta alphabet[count]
         *
         *     u32 delta table size
         *     delta table
         *
         *     delta compressed bytes
         *
         *     u64 main bit count
         *
         *     main compressed bytes
         *
         * The main compressed size is already stored
         * in the archive header, so the decoder can find
         * the end of the delta stream without another
         * delta-size field.
         */
        const uint64_t deltaFixed =
            archiveFixedSize(
                60,
                indexSize64,
                names.size(),
                dictionary.size());

        if (deltaFixed != UINT64_MAX) {

            deltaArchiveSize =
                deltaFixed;

            if (!addArchiveSize(
                    deltaArchiveSize,
                    4ULL +
                        globalTableData.size(),
                    deltaArchiveSize))
                deltaArchiveSize = UINT64_MAX;

            if (deltaArchiveSize != UINT64_MAX) {

                if (!addArchiveSize(
                        deltaArchiveSize,
                        2ULL +
                            deltaValues.size(),
                        deltaArchiveSize))
                    deltaArchiveSize = UINT64_MAX;
            }

            if (deltaArchiveSize != UINT64_MAX) {

                if (!addArchiveSize(
                        deltaArchiveSize,
                        4ULL +
                            deltaTableData.size(),
                        deltaArchiveSize))
                    deltaArchiveSize = UINT64_MAX;
            }

            if (deltaArchiveSize != UINT64_MAX) {

                if (!addArchiveSize(
                        deltaArchiveSize,
                        deltaWriter.data().size(),
                        deltaArchiveSize))
                    deltaArchiveSize = UINT64_MAX;
            }

            if (deltaArchiveSize != UINT64_MAX) {

                if (!addArchiveSize(
                        deltaArchiveSize,
                        8ULL,
                        deltaArchiveSize))
                    deltaArchiveSize = UINT64_MAX;
            }

            if (deltaArchiveSize != UINT64_MAX) {

                if (!addArchiveSize(
                        deltaArchiveSize,
                        deltaMainWriter.data().size(),
                        deltaArchiveSize))
                    deltaArchiveSize = UINT64_MAX;
            }
        }
    }

    /*
     * Automatic selection.
     *
     * Equal size -> global v6.
     *
     * This also means the new model is used only when
     * it really produces a smaller complete archive.
     */
    const bool useDelta =
        deltaCandidateValid &&
        deltaArchiveSize < globalArchiveSize;

    std::ofstream out(
        filename,
        std::ios::binary);

    if (!out)
        return false;

    uint64_t archiveOriginalSize = 0;

    for (const GtcArchiveFile& entry : files) {

        if (!addArchiveSize(
                archiveOriginalSize,
                entry.originalSize,
                archiveOriginalSize)) {

            return false;
        }
    }

    if (!useDelta) {

        /*
         * ----------------------------------------------------
         * Write v6 global archive.
         * ----------------------------------------------------
         */
        const std::vector<GtcArchiveFile>& index =
            globalIndex;

        GtcArchiveHeader header{};

        header.magic =
            GTC_MAGIC;

        header.version =
            GTC_ARCHIVE_VERSION_LEGACY;

        header.original_size =
            archiveOriginalSize;

        header.token_count =
            tokenCount;

        header.grammar_count =
            dictionary.size();

        header.token_count_in_stream =
            tokens.size();

        header.compressed_size =
            globalWriter.data().size();

        header.file_count =
            static_cast<uint32_t>(
                index.size());

        header.index_size =
            static_cast<uint32_t>(
                indexSize64);

        header.names_size =
            static_cast<uint32_t>(
                names.size());

        header.huffman_model_count =
            1;

        /*
         * v6 header: exactly 56 bytes.
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

        writeU32(
            out,
            header.huffman_model_count);

        if (!writeArchiveCommon(
                out,
                index,
                names,
                dictionary))
            return false;

        writeU32(
            out,
            static_cast<uint32_t>(
                globalTableData.size()));

        if (!globalTableData.empty()) {

            out.write(
                reinterpret_cast<const char*>(
                    globalTableData.data()),
                static_cast<std::streamsize>(
                    globalTableData.size()));
        }

        writeU64(
            out,
            globalWriter.bitCount());

        if (!globalWriter.data().empty()) {

            out.write(
                reinterpret_cast<const char*>(
                    globalWriter.data().data()),
                static_cast<std::streamsize>(
                    globalWriter.data().size()));
        }

        if (!out)
            return false;

        stats.compressedBits =
            globalWriter.bitCount();

        stats.compressedBytes =
            globalWriter.data().size();
    }
    else {

        /*
         * ----------------------------------------------------
         * Write v7 compact delta archive.
         * ----------------------------------------------------
         */
        const std::vector<GtcArchiveFile>& index =
            deltaIndex;

        GtcArchiveHeader header{};

        header.magic =
            GTC_MAGIC;

        header.version =
            GTC_ARCHIVE_VERSION;

        header.original_size =
            archiveOriginalSize;

        header.token_count =
            tokenCount;

        header.grammar_count =
            dictionary.size();

        header.token_count_in_stream =
            tokens.size();

        header.compressed_size =
            deltaMainWriter.data().size();

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
         * Only one global model is stored.
         */
        header.huffman_model_count =
            1;

        header.huffman_model_mode =
            GTC_HUFFMAN_MODE_DELTA;

        /*
         * v7 header: 60 bytes.
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

        writeU32(
            out,
            header.huffman_model_count);

        writeU32(
            out,
            header.huffman_model_mode);

        if (!writeArchiveCommon(
                out,
                index,
                names,
                dictionary))
            return false;

        /*
         * Global Huffman code-length table.
         */
        writeU32(
            out,
            static_cast<uint32_t>(
                globalTableData.size()));

        if (!globalTableData.empty()) {

            out.write(
                reinterpret_cast<const char*>(
                    globalTableData.data()),
                static_cast<std::streamsize>(
                    globalTableData.size()));
        }

        /*
         * Delta alphabet.
         *
         * u16 count
         * count signed int8 values
         */
        writeU16(
            out,
            static_cast<uint16_t>(
                deltaValues.size()));

        for (int8_t value :
             deltaValues) {

            const uint8_t byte =
                static_cast<uint8_t>(
                    value);

            out.write(
                reinterpret_cast<const char*>(
                    &byte),
                1);
        }

        /*
         * Delta Huffman table.
         */
        writeU32(
            out,
            static_cast<uint32_t>(
                deltaTableData.size()));

        if (!deltaTableData.empty()) {

            out.write(
                reinterpret_cast<const char*>(
                    deltaTableData.data()),
                static_cast<std::streamsize>(
                    deltaTableData.size()));
        }

        /*
         * Delta stream.
         */
        if (!deltaWriter.data().empty()) {

            out.write(
                reinterpret_cast<const char*>(
                    deltaWriter.data().data()),
                static_cast<std::streamsize>(
                    deltaWriter.data().size()));
        }

        /*
         * Main token stream bit count.
         *
         * The decoder uses the main compressed_size
         * from the header to locate the main stream at
         * the end of the archive.
         */
        writeU64(
            out,
            deltaMainWriter.bitCount());

        /*
         * Main token stream.
         */
        if (!deltaMainWriter.data().empty()) {

            out.write(
                reinterpret_cast<const char*>(
                    deltaMainWriter.data().data()),
                static_cast<std::streamsize>(
                    deltaMainWriter.data().size()));
        }

        if (!out)
            return false;

        stats.compressedBits =
            deltaMainWriter.bitCount();

        stats.compressedBytes =
            deltaMainWriter.data().size();
    }

    stats.originalSize =
        archiveOriginalSize;

    stats.finalTokenCount =
        tokens.size();

    stats.tokenCount =
        tokenCount;

    stats.grammarCount =
        dictionary.size();

    stats.fileCount =
        static_cast<uint32_t>(
            files.size());

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