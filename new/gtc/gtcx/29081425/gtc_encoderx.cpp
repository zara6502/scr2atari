#include "gtc_dictionary.h"
#include "gtc_encoder.h"
#include "gtc_encoderx.h"
#include "gtc_format.h"
#include "gtc_huffman.h"
#include "gtc_table.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstring>

namespace {


// ------------------------------------------------------------
// Little-endian output helpers
// ------------------------------------------------------------

static void writeU16(
    std::ofstream& out,
    uint16_t value)
{
    uint8_t b[2];

    b[0] = static_cast<uint8_t>(value);
    b[1] = static_cast<uint8_t>(value >> 8);

    out.write(
        reinterpret_cast<const char*>(b),
        2);
}


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
// Experimental word
// ------------------------------------------------------------

struct WordCandidate {
    std::string word;

    uint32_t count = 0;

    /*
     * First occurrence.
     *
     * Other occurrences are rediscovered during
     * the selection pass. This keeps the structure
     * relatively small.
     */
    size_t first = 0;
};


// ------------------------------------------------------------
// Hash for byte strings
// ------------------------------------------------------------

struct StringHash {
    size_t operator()(
        const std::string& s) const
    {
        /*
         * FNV-1a.
         *
         * The actual string is still stored in the map,
         * so hash collisions remain completely safe.
         */
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
//
// A node represents:
//
//     parent_token + byte
//
// Therefore every node is a valid GTC grammar token.
//
// Example:
//
//     "abc"
//
//     token("a")          = literal 'a'
//     token("ab")         = rule(a, b)
//     token("abc")        = rule(ab, c)
//
// Shared prefixes are reused.
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

        /*
         * Root represents the empty prefix.
         */
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

            /*
             * Parent token.
             *
             * For the root this can only happen
             * for the first byte, which is already
             * a literal token.
             */
            uint32_t parentToken =
                nodes_[node].token;

            uint32_t token;

            if (node == 0) {

                /*
                 * One-byte word is already a base token.
                 */
                token = static_cast<uint32_t>(c);
            }
            else {

                /*
                 * New grammar token:
                 *
                 *     parent + byte
                 */
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

    /*
     * For the first implementation we use the actual
     * byte string as the key.
     *
     * This is intentionally simple and exact.
     */
    std::unordered_map<
        std::string,
        WordCandidate,
        StringHash> candidates;

    /*
     * Do not reserve input.size() blindly.
     * For large files this could consume enormous
     * amounts of memory.
     */
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

    /*
     * Only repeated words are interesting.
     */
    result.reserve(
        candidates.size());

    for (auto& item : candidates) {

        if (item.second.count >= 2)
            result.push_back(
                std::move(item.second));
    }

    /*
     * Most frequent first.
     *
     * This determines which words get the
     * available positions when candidates overlap.
     */
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

        /*
         * Scan the input again for this exact word.
         *
         * We only accept currently free occurrences.
         */
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

        /*
         * A candidate that lost almost all of its
         * occurrences because of previous candidates
         * simply disappears from the result.
         */
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
// Write GTC v4
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
     * Grammar rule references are uint16_t in the
     * file format.
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
     * Header.
     */
    writeU32(out, header.magic);
    writeU32(out, header.version);
    writeU64(out, header.original_size);
    writeU32(out, header.token_count);
    writeU32(out, header.grammar_count);
    writeU64(out, header.token_count_in_stream);
    writeU64(out, header.compressed_size);

    /*
     * Grammar.
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

    /*
     * Huffman code-length table.
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
     * Huffman stream.
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
         *
         * We keep this separate from the dictionary.
         */
        std::vector<
            std::pair<size_t, std::string>>
            selected;

        /*
         * ----------------------------------------------------
         * Main experimental search:
         *
         *     32
         *     31
         *     ...
         *      2
         *
         * Longer words get first access to the input.
         * Once a position is occupied it cannot be used
         * by a shorter word.
         * ----------------------------------------------------
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
         * ----------------------------------------------------
         * Build grammar.
         *
         * A trie shares common prefixes between words.
         * ----------------------------------------------------
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

                /*
                 * Check before constructing grammar.
                 *
                 * Every rule adds one dictionary token.
                 */
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
         * ----------------------------------------------------
         * Convert occupied words + remaining bytes
         * into the final token stream.
         * ----------------------------------------------------
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

if (!dictionary.reorderForEncoding(
        sequence)) {

    std::fprintf(
        stderr,
        "error: cannot reorder grammar\n");

    return false;
}

        /*
         * ----------------------------------------------------
         * Final GTC v4 writer.
         * ----------------------------------------------------
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
            "original       : %llu bytes\n"
            "final tokens   : %llu\n"
            "tokens         : %u\n"
            "grammar rules  : %u\n"
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
    };