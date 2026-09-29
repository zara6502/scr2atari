#include "gtc_dictionary.h"
#include "gtc_encoder.h"
#include "gtc_encoderx.h"
#include "gtc_format.h"
#include "gtc_huffman.h"
#include "gtc_table.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {


// ------------------------------------------------------------
// Little-endian output helpers
// ------------------------------------------------------------

static void writeU32(
    std::ofstream& out,
    uint32_t value)
{
    uint8_t b[4];

    b[0] = static_cast<uint8_t>(value);
    b[1] = static_cast<uint8_t>(value >> 8);
    b[2] = static_cast<uint8_t>(value >> 16);
    b[3] = static_cast<uint8_t>(value >> 24);

    out.write(
        reinterpret_cast<const char*>(b),
        4);
}


static void writeU64(
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


// ------------------------------------------------------------
// Compact grammar encoding
//
// Grammar values are uint16_t.
//
// Encoding:
//
//     delta = current - previous
//     zigzag(delta)
//     unsigned varint
//
// Initial previous value is zero.
//
// This is the physical encoding corresponding to the
// virtual cost measured by grammar tiebreak2/reorder.
// ------------------------------------------------------------

static uint32_t zigzag(
    int32_t value)
{
    return
        (static_cast<uint32_t>(value) << 1) ^
        static_cast<uint32_t>(value >> 31);
}


static uint32_t varintSize(
    uint32_t value)
{
    uint32_t n = 1;

    while (value >= 128) {
        value >>= 7;
        ++n;
    }

    return n;
}


static bool writeVarint(
    std::ofstream& out,
    uint32_t value)
{
    while (value >= 0x80u) {

        const uint8_t byte =
            static_cast<uint8_t>(
                (value & 0x7Fu) | 0x80u);

        out.put(
            static_cast<char>(byte));

        if (!out)
            return false;

        value >>= 7;
    }

    out.put(
        static_cast<char>(
            static_cast<uint8_t>(value)));

    return static_cast<bool>(out);
}


// ------------------------------------------------------------
// Experimental word
// ------------------------------------------------------------

struct WordCandidate {
    std::string word;

    uint32_t count = 0;

    size_t first = 0;
};


// ------------------------------------------------------------
// Hash for byte strings
// ------------------------------------------------------------

struct StringHash {
    size_t operator()(
        const std::string& s) const
    {
        uint64_t h =
            1469598103934665603ULL;

        for (unsigned char c : s) {
            h ^= c;
            h *= 1099511628211ULL;
        }

        return static_cast<size_t>(h);
    }
};


// ------------------------------------------------------------
// Trie used to construct grammar rules
// ------------------------------------------------------------

struct TrieNode {
    uint32_t token = 0;

    std::unordered_map<uint8_t, uint32_t> next;
};


class WordTrie {
public:

    WordTrie()
    {
        TrieNode root;

        root.token = 0;

        nodes_.push_back(
            std::move(root));
    }


    uint32_t addWord(
        const std::string& word,
        GtcDictionary& dictionary)
    {
        uint32_t node = 0;

        for (unsigned char c : word) {

            auto it =
                nodes_[node].next.find(c);

            if (it != nodes_[node].next.end()) {
                node = it->second;
                continue;
            }

            uint32_t parentToken =
                nodes_[node].token;

            uint32_t token;

            if (node == 0) {

                token =
                    static_cast<uint32_t>(c);
            }
            else {

                token =
                    dictionary.addRule(
                        parentToken,
                        static_cast<uint32_t>(c));
            }

            TrieNode child;

            child.token = token;

            const uint32_t childIndex =
                static_cast<uint32_t>(
                    nodes_.size());

            nodes_.push_back(
                std::move(child));

            nodes_[node].next.emplace(
                c,
                childIndex);

            node = childIndex;
        }

        return nodes_[node].token;
    }


private:
    std::vector<TrieNode> nodes_;
};


// ------------------------------------------------------------
// Range occupancy
// ------------------------------------------------------------

class Occupancy {
public:

    explicit Occupancy(
        size_t size)
        : bits_(
              (size + 63) / 64,
              0)
    {
    }


    bool freeRange(
        size_t start,
        size_t length) const
    {
        const size_t end =
            start + length;

        const size_t firstWord =
            start >> 6;

        const size_t lastWord =
            (end - 1) >> 6;

        if (firstWord == lastWord) {

            const uint32_t firstBit =
                static_cast<uint32_t>(
                    start & 63);

            const uint32_t lastBit =
                static_cast<uint32_t>(
                    (end - 1) & 63);

            uint64_t mask;

            if (lastBit == 63)
                mask =
                    ~0ULL <<
                    firstBit;
            else
                mask =
                    ((1ULL <<
                      (lastBit - firstBit + 1))
                     - 1ULL)
                    << firstBit;

            return
                (bits_[firstWord] & mask) == 0;
        }

        /*
         * First partial word.
         */
        {
            const uint32_t bit =
                static_cast<uint32_t>(
                    start & 63);

            const uint64_t mask =
                ~0ULL << bit;

            if ((bits_[firstWord] & mask) != 0)
                return false;
        }

        /*
         * Full words.
         */
        for (size_t word =
                 firstWord + 1;
             word < lastWord;
             ++word) {

            if (bits_[word] != 0)
                return false;
        }

        /*
         * Last partial word.
         */
        {
            const uint32_t bit =
                static_cast<uint32_t>(
                    (end - 1) & 63);

            const uint64_t mask =
                bit == 63
                    ? ~0ULL
                    : ((1ULL << (bit + 1)) - 1ULL);

            if ((bits_[lastWord] & mask) != 0)
                return false;
        }

        return true;
    }


    void occupy(
        size_t start,
        size_t length)
    {
        const size_t end =
            start + length;

        const size_t firstWord =
            start >> 6;

        const size_t lastWord =
            (end - 1) >> 6;

        if (firstWord == lastWord) {

            const uint32_t firstBit =
                static_cast<uint32_t>(
                    start & 63);

            const uint32_t lastBit =
                static_cast<uint32_t>(
                    (end - 1) & 63);

            uint64_t mask;

            if (lastBit == 63)
                mask =
                    ~0ULL << firstBit;
            else
                mask =
                    ((1ULL <<
                      (lastBit - firstBit + 1))
                     - 1ULL)
                    << firstBit;

            bits_[firstWord] |= mask;
            return;
        }

        /*
         * First partial word.
         */
        {
            const uint32_t bit =
                static_cast<uint32_t>(
                    start & 63);

            bits_[firstWord] |=
                ~0ULL << bit;
        }

        /*
         * Full words.
         */
        for (size_t word =
                 firstWord + 1;
             word < lastWord;
             ++word) {

            bits_[word] = ~0ULL;
        }

        /*
         * Last partial word.
         */
        {
            const uint32_t bit =
                static_cast<uint32_t>(
                    (end - 1) & 63);

            const uint64_t mask =
                bit == 63
                    ? ~0ULL
                    : ((1ULL << (bit + 1)) - 1ULL);

            bits_[lastWord] |= mask;
        }
    }


private:
    std::vector<uint64_t> bits_;
};


// ------------------------------------------------------------
// Read complete file
// ------------------------------------------------------------

static bool readFile(
    const std::string& filename,
    std::vector<uint8_t>& data)
{
    std::ifstream file(
        filename,
        std::ios::binary | std::ios::ate);

    if (!file)
        return false;

    const std::streamsize size =
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


// ------------------------------------------------------------
// Find candidates for one word length
// ------------------------------------------------------------

static void findCandidates(
    const std::vector<uint8_t>& input,
    const Occupancy& occupied,
    size_t length,
    std::vector<WordCandidate>& result)
{
    result.clear();

    if (input.size() < length)
        return;

    std::unordered_map<
        std::string,
        WordCandidate,
        StringHash> candidates;

    const size_t possible =
        input.size() - length + 1;

    const size_t reserveCount =
        std::min<size_t>(
            possible,
            1024 * 1024);

    candidates.reserve(
        reserveCount);

    for (size_t pos = 0;
         pos < possible;
         ++pos) {

        if (!occupied.freeRange(
                pos,
                length))
            continue;

        std::string word(
            reinterpret_cast<const char*>(
                input.data() + pos),
            length);

        auto it =
            candidates.find(word);

        if (it == candidates.end()) {

            WordCandidate candidate;

            candidate.word =
                std::move(word);

            candidate.count = 1;
            candidate.first = pos;

            candidates.emplace(
                candidate.word,
                std::move(candidate));
        }
        else {
            ++it->second.count;
        }
    }

    result.reserve(
        candidates.size());

    for (auto& item : candidates) {

        if (item.second.count >= 2)
            result.push_back(
                std::move(item.second));
    }

    std::sort(
        result.begin(),
        result.end(),
        [](const WordCandidate& a,
           const WordCandidate& b) {

            if (a.count != b.count)
                return a.count > b.count;

            if (a.word.size() != b.word.size())
                return a.word.size() > b.word.size();

            return a.first < b.first;
        });
}


// ------------------------------------------------------------
// Select occurrences for one length
// ------------------------------------------------------------

static void selectCandidates(
    const std::vector<uint8_t>& input,
    size_t length,
    const std::vector<WordCandidate>& candidates,
    Occupancy& occupied,
    std::vector<std::pair<size_t, std::string>>& selected)
{
    for (const WordCandidate& candidate :
         candidates) {

        uint32_t count = 0;

        for (size_t pos = 0;
             pos + length <= input.size();
             ++pos) {

            if (!occupied.freeRange(
                    pos,
                    length))
                continue;

            if (std::memcmp(
                    input.data() + pos,
                    candidate.word.data(),
                    length) != 0)
                continue;

            selected.emplace_back(
                pos,
                candidate.word);

            occupied.occupy(
                pos,
                length);

            ++count;
        }

        (void)count;
    }
}


// ------------------------------------------------------------
// Build final token stream
// ------------------------------------------------------------

struct SelectedOccurrence {
    size_t position = 0;
    size_t length = 0;
    uint32_t token = 0;
};


static bool buildTokenStream(
    const std::vector<uint8_t>& input,
    const std::vector<SelectedOccurrence>& occurrences,
    std::vector<uint32_t>& sequence)
{
    sequence.clear();

    sequence.reserve(
        input.size());

    size_t occurrenceIndex = 0;

    for (size_t pos = 0;
         pos < input.size();) {

        if (occurrenceIndex <
                occurrences.size() &&
            occurrences[occurrenceIndex].position ==
                pos) {

            const SelectedOccurrence&
                occurrence =
                    occurrences[occurrenceIndex];

            sequence.push_back(
                occurrence.token);

            pos += occurrence.length;

            ++occurrenceIndex;
            continue;
        }

        sequence.push_back(
            input[pos]);

        ++pos;
    }

    return occurrenceIndex ==
           occurrences.size();
}

// ------------------------------------------------------------
// Diagnostic: verify physical compact grammar cost
// ------------------------------------------------------------

static void diagnoseCompactGrammar(
    const GtcDictionary& dictionary)
{
    uint64_t leftBytes = 0;
    uint64_t rightBytes = 0;

    uint64_t leftCount[6] = {};
    uint64_t rightCount[6] = {};

    uint32_t previousLeft = 0;
    uint32_t previousRight = 0;

    const uint32_t count =
        dictionary.size();

    for (uint32_t i = 0;
         i < count;
         ++i) {

        const GtcRule& rule =
            dictionary.rule(
                GTC_BASE_TOKENS + i);

        const uint32_t left =
            static_cast<uint32_t>(
                rule.left);

        const uint32_t right =
            static_cast<uint32_t>(
                rule.right);

        const int32_t leftDelta =
            static_cast<int32_t>(left) -
            static_cast<int32_t>(previousLeft);

        const int32_t rightDelta =
            static_cast<int32_t>(right) -
            static_cast<int32_t>(previousRight);

        const uint32_t leftValue =
            zigzag(leftDelta);

        const uint32_t rightValue =
            zigzag(rightDelta);

        const uint32_t leftSize =
            varintSize(leftValue);

        const uint32_t rightSize =
            varintSize(rightValue);

        leftBytes += leftSize;
        rightBytes += rightSize;

        if (leftSize <= 5)
            ++leftCount[leftSize];

        if (rightSize <= 5)
            ++rightCount[rightSize];

        previousLeft = left;
        previousRight = right;
    }

    const uint64_t total =
        leftBytes + rightBytes;

    const uint64_t oldSize =
        static_cast<uint64_t>(count) * 4ULL;

    std::printf(
        "\n"
        "=== COMPACT GRAMMAR DIAGNOSTIC ===\n"
        "\n"
        "grammar rules   : %u\n"
        "\n"
        "LEFT\n"
        "  1 byte        : %llu\n"
        "  2 bytes       : %llu\n"
        "  3 bytes       : %llu\n"
        "  4 bytes       : %llu\n"
        "  5 bytes       : %llu\n"
        "  total bytes   : %llu\n"
        "\n"
        "RIGHT\n"
        "  1 byte        : %llu\n"
        "  2 bytes       : %llu\n"
        "  3 bytes       : %llu\n"
        "  4 bytes       : %llu\n"
        "  5 bytes       : %llu\n"
        "  total bytes   : %llu\n"
        "\n"
        "COMBINED\n"
        "  compact bytes : %llu\n"
        "  old bytes     : %llu\n"
        "  saving        : %.3f%%\n"
        "\n",

        count,

        static_cast<unsigned long long>(
            leftCount[1]),
        static_cast<unsigned long long>(
            leftCount[2]),
        static_cast<unsigned long long>(
            leftCount[3]),
        static_cast<unsigned long long>(
            leftCount[4]),
        static_cast<unsigned long long>(
            leftCount[5]),
        static_cast<unsigned long long>(
            leftBytes),

        static_cast<unsigned long long>(
            rightCount[1]),
        static_cast<unsigned long long>(
            rightCount[2]),
        static_cast<unsigned long long>(
            rightCount[3]),
        static_cast<unsigned long long>(
            rightCount[4]),
        static_cast<unsigned long long>(
            rightCount[5]),
        static_cast<unsigned long long>(
            rightBytes),

        static_cast<unsigned long long>(
            total),
        static_cast<unsigned long long>(
            oldSize),

        oldSize != 0
            ? 100.0 *
              (1.0 -
               static_cast<double>(total) /
               static_cast<double>(oldSize))
            : 0.0);

    // --------------------------------------------------------
    // First 30 rules
    // --------------------------------------------------------

    std::printf(
        "FIRST 30 RULES\n"
        "\n"
        "#        L    prevL    dL    zL   nL"
        "        R    prevR    dR    zR   nR\n");

    previousLeft = 0;
    previousRight = 0;

    const uint32_t dumpCount =
        std::min<uint32_t>(
            count,
            30);

    for (uint32_t i = 0;
         i < dumpCount;
         ++i) {

        const GtcRule& rule =
            dictionary.rule(
                GTC_BASE_TOKENS + i);

        const uint32_t left =
            static_cast<uint32_t>(
                rule.left);

        const uint32_t right =
            static_cast<uint32_t>(
                rule.right);

        const int32_t leftDelta =
            static_cast<int32_t>(left) -
            static_cast<int32_t>(previousLeft);

        const int32_t rightDelta =
            static_cast<int32_t>(right) -
            static_cast<int32_t>(previousRight);

        const uint32_t leftZ =
            zigzag(leftDelta);

        const uint32_t rightZ =
            zigzag(rightDelta);

        const uint32_t leftSize =
            varintSize(leftZ);

        const uint32_t rightSize =
            varintSize(rightZ);

        std::printf(
            "%-4u  %5u %7u %6d %6u %3u"
            "   %5u %7u %6d %6u %3u\n",

            i,

            left,
            previousLeft,
            leftDelta,
            leftZ,
            leftSize,

            right,
            previousRight,
            rightDelta,
            rightZ,
            rightSize);

        previousLeft = left;
        previousRight = right;
    }

    std::printf("\n");
}

// ------------------------------------------------------------
// Write compact grammar
//
// IMPORTANT:
//
// dictionary has already been reordered by
// GtcDictionary::reorderForEncoding().
//
// Therefore the rule order here is exactly the order
// selected by the DEPS+LEFT optimizer.
//
// The physical representation is:
//
//     left_delta
//     right_delta
//
// where both values are:
//
//     signed delta
//       -> zigzag
//       -> varint
//
// This replaces the old:
//
//     uint16 left
//     uint16 right
//
// 4 bytes/rule representation.
// ------------------------------------------------------------

static bool writeCompactGrammar(
    std::ofstream& out,
    const GtcDictionary& dictionary,
    uint64_t& grammarBytes,
    uint64_t& grammarBits)
{
    grammarBytes = 0;
    grammarBits = 0;

    uint32_t previousLeft = 0;
    uint32_t previousRight = 0;

    const uint32_t count =
        static_cast<uint32_t>(
            dictionary.size());

    for (uint32_t i = 0;
         i < count;
         ++i) {

        const GtcRule& rule =
            dictionary.rule(
                GTC_BASE_TOKENS + i);

        /*
         * Rule references are uint16_t by design.
         */
        const uint32_t left =
            static_cast<uint32_t>(
                rule.left);

        const uint32_t right =
            static_cast<uint32_t>(
                rule.right);

        const int32_t leftDelta =
            static_cast<int32_t>(left) -
            static_cast<int32_t>(previousLeft);

        const int32_t rightDelta =
            static_cast<int32_t>(right) -
            static_cast<int32_t>(previousRight);

        const uint32_t leftValue =
            zigzag(leftDelta);

        const uint32_t rightValue =
            zigzag(rightDelta);

        if (!writeVarint(
                out,
                leftValue))
            return false;

        if (!writeVarint(
                out,
                rightValue))
            return false;

        grammarBytes +=
            varintSize(leftValue);

        grammarBytes +=
            varintSize(rightValue);

        previousLeft = left;
        previousRight = right;
    }

    grammarBits =
        grammarBytes * 8ULL;

    return true;
}


// ------------------------------------------------------------
// Write GTC X
// ------------------------------------------------------------

static bool writeGtcX(
    const std::string& filename,
    uint64_t originalSize,
    const GtcDictionary& dictionary,
    const std::vector<uint32_t>& sequence,
    GtcEncodeStats& stats)
{
    const uint32_t tokenCount =
        GTC_BASE_TOKENS +
        dictionary.size();

    /*
     * Grammar rule references are uint16_t.
     */
    if (tokenCount > 65536u)
        return false;

    std::vector<uint64_t> frequencies(
        tokenCount,
        0);

    for (uint32_t token : sequence) {

        if (token >= tokenCount)
            return false;

        ++frequencies[token];
    }

    GtcHuffman huffman;

    if (!huffman.build(
            frequencies))
        return false;

    BitWriter writer;

    huffman.encode(
        sequence,
        writer);

    writer.flush();

    const std::vector<uint8_t>&
        compressed =
            writer.data();

    /*
     * Compress the Huffman code-length table
     * exactly like the normal GTC encoder.
     */
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

    /*
     * --------------------------------------------------------
     * Header
     * --------------------------------------------------------
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
     * --------------------------------------------------------
     * Compact grammar
     *
     * This is the important part.
     *
     * The old implementation used:
     *
     *     4 bytes/rule
     *
     * The new implementation uses:
     *
     *     delta + zigzag + varint
     *
     * on both left and right.
     * --------------------------------------------------------
     */
    uint64_t grammarBytes = 0;
    uint64_t grammarBits = 0;

    if (!writeCompactGrammar(
            out,
            dictionary,
            grammarBytes,
            grammarBits)) {

        return false;
    }

    /*
     * --------------------------------------------------------
     * Huffman code-length table
     * --------------------------------------------------------
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
     * --------------------------------------------------------
     * Valid bit count
     * --------------------------------------------------------
     */
    writeU64(
        out,
        writer.bitCount());

    /*
     * --------------------------------------------------------
     * Huffman stream
     * --------------------------------------------------------
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

    /*
     * --------------------------------------------------------
     * Statistics
     * --------------------------------------------------------
     *
     * compressedBytes / compressedBits remain the Huffman
     * statistics, as in the previous implementation.
     *
     * grammarBytes is deliberately not mixed into those
     * fields because GtcEncodeStats is shared with the
     * normal encoder.
     * --------------------------------------------------------
     */

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

    /*
     * Useful diagnostic. This lets us immediately compare
     * the real physical grammar size against tiebreak2.
     */
    std::printf(
        "grammar bytes  : %llu\n"
        "grammar saving : %.3f%%\n",
        static_cast<unsigned long long>(
            grammarBytes),
        dictionary.size() != 0
            ? 100.0 *
              (1.0 -
               static_cast<double>(grammarBytes) /
               (static_cast<double>(dictionary.size()) * 4.0))
            : 0.0);

    (void)grammarBits;

    return true;
}


} // namespace


// ============================================================
// GtcEncoderX
// ============================================================

bool GtcEncoderX::encodeFile(
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

    /*
     * Empty file cannot produce a Huffman model
     * with the current GtcHuffman implementation.
     */
    if (input.empty()) {

        std::fprintf(
            stderr,
            "error: empty input is not supported\n");

        return false;
    }

    Occupancy occupied(
        input.size());

    /*
     * position -> word.
     */
    std::vector<
        std::pair<size_t, std::string>>
        selected;

    /*
     * --------------------------------------------------------
     * Main experimental search:
     *
     *     32
     *     31
     *     ...
     *      2
     *
     * Longer words get first access to the input.
     * --------------------------------------------------------
     */
    const size_t maxLength =
        std::min<size_t>(
            32,
            input.size());

    for (size_t length =
             maxLength;
         length >= 2;
         --length) {

        std::vector<WordCandidate>
            candidates;

        findCandidates(
            input,
            occupied,
            length,
            candidates);

        if (candidates.empty())
            continue;

        selectCandidates(
            input,
            length,
            candidates,
            occupied,
            selected);
    }

    /*
     * Sort selected occurrences by position.
     */
    std::sort(
        selected.begin(),
        selected.end(),
        [](const auto& a,
           const auto& b) {

            return a.first < b.first;
        });

    /*
     * --------------------------------------------------------
     * Build grammar.
     *
     * A trie shares common prefixes between words.
     * --------------------------------------------------------
     */
    GtcDictionary dictionary;

    WordTrie trie;

    std::unordered_map<
        std::string,
        uint32_t,
        StringHash> wordTokens;

    wordTokens.reserve(
        selected.size());

    std::vector<
        SelectedOccurrence>
        occurrences;

    occurrences.reserve(
        selected.size());

    for (const auto& item :
         selected) {

        const std::string& word =
            item.second;

        auto it =
            wordTokens.find(word);

        uint32_t token;

        if (it != wordTokens.end()) {

            token =
                it->second;
        }
        else {

            token =
                trie.addWord(
                    word,
                    dictionary);

            if (GTC_BASE_TOKENS +
                    dictionary.size() >
                65536u) {

                std::fprintf(
                    stderr,
                    "error: experimental grammar is too large\n");

                return false;
            }

            wordTokens.emplace(
                word,
                token);
        }

        SelectedOccurrence occurrence;

        occurrence.position =
            item.first;

        occurrence.length =
            word.size();

        occurrence.token =
            token;

        occurrences.push_back(
            occurrence);
    }

    /*
     * --------------------------------------------------------
     * Convert occupied words + remaining bytes
     * into the final token stream.
     * --------------------------------------------------------
     */
    std::vector<uint32_t> sequence;

    if (!buildTokenStream(
            input,
            occurrences,
            sequence)) {

        std::fprintf(
            stderr,
            "error: invalid experimental tokenization\n");

        return false;
    }

    /*
     * --------------------------------------------------------
     * Grammar optimization.
     *
     * This is the DEPS+LEFT reorder selected by
     * grammar/tiebreak2 experiments.
     *
     * It does two things:
     *
     *   1. chooses the grammar emission order;
     *   2. remaps grammar token IDs in both rules and
     *      the token stream.
     *
     * The writer below then physically encodes that
     * reordered grammar using delta+zigzag+varint.
     * --------------------------------------------------------
     */
if (!dictionary.reorderForEncoding(
        sequence)) {

    std::fprintf(
        stderr,
        "error: cannot reorder grammar\n");

    return false;
}

diagnoseCompactGrammar(
    dictionary);

    /*
     * --------------------------------------------------------
     * Final GTC X writer.
     * --------------------------------------------------------
     */
    if (!writeGtcX(
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

    /*
     * Useful experimental diagnostics.
     */
    std::printf(
        "GTC X\n"
        "\n"
        "original        : %llu bytes\n"
        "final tokens    : %llu\n"
        "tokens          : %u\n"
        "grammar rules   : %u\n"
        "dictionary words: %llu\n"
        "Huffman bytes   : %llu\n"
        "Huffman bits    : %llu\n",
        static_cast<unsigned long long>(
            stats.originalSize),
        static_cast<unsigned long long>(
            stats.finalTokenCount),
        stats.tokenCount,
        stats.grammarCount,
        static_cast<unsigned long long>(
            wordTokens.size()),
        static_cast<unsigned long long>(
            stats.compressedBytes),
        static_cast<unsigned long long>(
            stats.compressedBits));

    return true;
}