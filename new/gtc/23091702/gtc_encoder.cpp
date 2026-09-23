#include "gtc_encoder.h"
#include "gtc_format.h"
#include "gtc_huffman.h"

#include <queue>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <cstdio>
#include <fstream>

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
static uint32_t bitsForValue(uint32_t value)
{
    uint32_t bits = 0;

    do {
        ++bits;
        value >>= 1;
    } while (value != 0);

    return bits;
}
static uint32_t bitsForToken(uint32_t token)
{
    uint32_t bits = 0;

    do {
        ++bits;
        token >>= 1;
    } while (token != 0);

    return bits;
}

struct OperandHuffmanNode {
    uint64_t frequency;
    int left;
    int right;
};

static uint32_t operandHuffmanLength(
    const std::vector<uint64_t>& frequencies,
    uint64_t& totalBits,
    uint64_t& symbolCount)
{
    totalBits = 0;
    symbolCount = 0;

    struct QueueNode {
        uint64_t frequency;
        int index;

        bool operator>(
            const QueueNode& other) const
        {
            return frequency > other.frequency;
        }
    };

    std::vector<OperandHuffmanNode> nodes;
    nodes.reserve(frequencies.size() * 2);

    std::priority_queue<
        QueueNode,
        std::vector<QueueNode>,
        std::greater<QueueNode>
    > queue;

    for (uint32_t symbol = 0;
         symbol < frequencies.size();
         ++symbol) {

        if (frequencies[symbol] == 0)
            continue;

        const int index =
            static_cast<int>(nodes.size());

        nodes.push_back({
            frequencies[symbol],
            -1,
            -1
        });

        queue.push({
            frequencies[symbol],
            index
        });

        symbolCount += frequencies[symbol];
    }

    if (queue.empty())
        return 0;

    /*
     * Only one symbol.
     * Huffman code can be represented by one bit
     * per occurrence in an actual bitstream.
     */
    if (queue.size() == 1) {
        totalBits = symbolCount;
        return 1;
    }

    /*
     * Build Huffman tree.
     */
    while (queue.size() > 1) {
        const QueueNode a = queue.top();
        queue.pop();

        const QueueNode b = queue.top();
        queue.pop();

        const int index =
            static_cast<int>(nodes.size());

        nodes.push_back({
            a.frequency + b.frequency,
            a.index,
            b.index
        });

        queue.push({
            a.frequency + b.frequency,
            index
        });
    }

    const int root =
        queue.top().index;

    /*
     * Calculate code length for every leaf.
     */
    std::vector<uint32_t> lengths(
        nodes.size(),
        0);

    std::vector<std::pair<int, uint32_t>> stack;
    stack.reserve(nodes.size());

    stack.push_back({
        root,
        0
    });

    while (!stack.empty()) {
        const auto item =
            stack.back();

        stack.pop_back();

        const int index = item.first;
        const uint32_t depth = item.second;

        const OperandHuffmanNode& node =
            nodes[index];

        if (node.left < 0 &&
            node.right < 0) {

            lengths[index] = depth;
            totalBits +=
                node.frequency * depth;

            continue;
        }

        if (node.left >= 0) {
            stack.push_back({
                node.left,
                depth + 1
            });
        }

        if (node.right >= 0) {
            stack.push_back({
                node.right,
                depth + 1
            });
        }
    }

    uint32_t maxLength = 0;

    for (uint32_t length : lengths) {
        if (length > maxLength)
            maxLength = length;
    }

    return maxLength;
}


static void analyzeOperandHuffman(
    const GtcDictionary& dictionary)
{
    const uint32_t grammarCount =
        dictionary.size();

    const uint32_t tokenCount =
        GTC_BASE_TOKENS + grammarCount;

    std::vector<uint64_t> allFreq(tokenCount, 0);
    std::vector<uint64_t> leftFreq(tokenCount, 0);
    std::vector<uint64_t> rightFreq(tokenCount, 0);
    std::vector<uint64_t> grammarFreq(tokenCount, 0);

    uint64_t grammarOperandCount = 0;

    for (uint32_t i = 0;
         i < grammarCount;
         ++i) {

        const uint32_t token =
            GTC_BASE_TOKENS + i;

        const GtcRule& rule =
            dictionary.rule(token);

        ++allFreq[rule.left];
        ++allFreq[rule.right];

        ++leftFreq[rule.left];
        ++rightFreq[rule.right];

        if (rule.left >= GTC_BASE_TOKENS) {
            ++grammarFreq[rule.left];
            ++grammarOperandCount;
        }

        if (rule.right >= GTC_BASE_TOKENS) {
            ++grammarFreq[rule.right];
            ++grammarOperandCount;
        }
    }

    struct Result {
        const char* name;
        uint64_t count;
        uint64_t bits;
        uint64_t bytes16;
        uint64_t huffmanBytes;
        uint64_t savingBytes;
        double averageBits;
    };

    auto analyze =
        [](const char* name,
           const std::vector<uint64_t>& frequencies,
           uint64_t count) -> Result {

        uint64_t bits = 0;
        uint64_t symbolCount = 0;

        operandHuffmanLength(
            frequencies,
            bits,
            symbolCount);

        const uint64_t bytes16 =
            (count * 16 + 7) / 8;

        const uint64_t huffmanBytes =
            (bits + 7) / 8;

        const uint64_t saving =
            bytes16 > huffmanBytes
                ? bytes16 - huffmanBytes
                : 0;

        const double average =
            count != 0
                ? static_cast<double>(bits) /
                  static_cast<double>(count)
                : 0.0;

        return {
            name,
            count,
            bits,
            bytes16,
            huffmanBytes,
            saving,
            average
        };
    };

    const uint64_t totalOperands =
        static_cast<uint64_t>(grammarCount) * 2;

    const uint64_t leftCount =
        grammarCount;

    const uint64_t rightCount =
        grammarCount;

    Result results[] = {
        analyze(
            "all operands",
            allFreq,
            totalOperands),

        analyze(
            "left operands",
            leftFreq,
            leftCount),

        analyze(
            "right operands",
            rightFreq,
            rightCount),

        analyze(
            "grammar operands",
            grammarFreq,
            grammarOperandCount)
    };

    std::cout
        << "\nOperand Huffman analysis\n"
        << "------------------------\n";

    std::cout
        << std::left
        << std::setw(20) << "category"
        << std::setw(10) << "count"
        << std::setw(14) << "bits"
        << std::setw(14) << "avg bits"
        << std::setw(14) << "16-bit bytes"
        << std::setw(14) << "Huffman bytes"
        << std::setw(14) << "saving"
        << '\n';

    for (const Result& result : results) {
        std::cout
            << std::left
            << std::setw(20)
            << result.name

            << std::setw(10)
            << result.count

            << std::setw(14)
            << result.bits

            << std::setw(14)
            << std::fixed
            << std::setprecision(3)
            << result.averageBits

            << std::setw(14)
            << result.bytes16

            << std::setw(14)
            << result.huffmanBytes

            << std::setw(14)
            << result.savingBytes

            << '\n';
    }

    /*
     * Detailed grammar-only result.
     */

    Result grammarResult =
        results[3];

    std::cout
        << "\nGrammar operands\n"
        << "----------------\n";

    std::cout
        << "references        : "
        << grammarResult.count
        << '\n';

    std::cout
        << "fixed 16-bit      : "
        << grammarResult.bytes16
        << " bytes\n";

    std::cout
        << "Huffman bits      : "
        << grammarResult.bits
        << '\n';

    std::cout
        << "Huffman payload   : "
        << grammarResult.huffmanBytes
        << " bytes\n";

    std::cout
        << "average bits      : "
        << std::fixed
        << std::setprecision(3)
        << grammarResult.averageBits
        << '\n';

    std::cout
        << "saving             : "
        << grammarResult.savingBytes
        << " bytes\n";

    if (grammarResult.bytes16 != 0) {
        std::cout
            << "saving percent     : "
            << std::fixed
            << std::setprecision(2)
            << (100.0 *
                grammarResult.savingBytes /
                grammarResult.bytes16)
            << "%\n";
    }
}
static void analyzeOperandFrequency(
    const GtcDictionary& dictionary)
{
    const uint32_t grammarCount =
        dictionary.size();

    const uint32_t tokenCount =
        GTC_BASE_TOKENS + grammarCount;

    std::vector<uint64_t> leftFreq(tokenCount, 0);
    std::vector<uint64_t> rightFreq(tokenCount, 0);
    std::vector<uint64_t> totalFreq(tokenCount, 0);

    uint64_t totalOperands = 0;
    uint64_t grammarOperands = 0;

    uint64_t bbOperands = 0;
    uint64_t bgOperands = 0;
    uint64_t gbOperands = 0;
    uint64_t ggOperands = 0;

    for (uint32_t i = 0; i < grammarCount; ++i) {
        const uint32_t token =
            GTC_BASE_TOKENS + i;

        const GtcRule& rule =
            dictionary.rule(token);

        ++leftFreq[rule.left];
        ++rightFreq[rule.right];

        ++totalFreq[rule.left];
        ++totalFreq[rule.right];

        totalOperands += 2;

        const bool leftGrammar =
            rule.left >= GTC_BASE_TOKENS;

        const bool rightGrammar =
            rule.right >= GTC_BASE_TOKENS;

        if (!leftGrammar && !rightGrammar)
            ++bbOperands;
        else if (!leftGrammar && rightGrammar)
            ++bgOperands;
        else if (leftGrammar && !rightGrammar)
            ++gbOperands;
        else
            ++ggOperands;

        if (leftGrammar)
            ++grammarOperands;

        if (rightGrammar)
            ++grammarOperands;
    }

    struct Entry {
        uint32_t token;
        uint64_t count;
    };

    std::vector<Entry> entries;
    entries.reserve(tokenCount);

    for (uint32_t token = 0; token < tokenCount; ++token) {
        if (totalFreq[token] != 0) {
            entries.push_back({
                token,
                totalFreq[token]
            });
        }
    }

    std::sort(
        entries.begin(),
        entries.end(),
        [](const Entry& a, const Entry& b) {
            if (a.count != b.count)
                return a.count > b.count;

            return a.token < b.token;
        });

    std::cout
        << "\nOperand frequency analysis\n"
        << "---------------------------\n";

    std::cout
        << "grammar rules       : "
        << grammarCount << '\n';

    std::cout
        << "total operands      : "
        << totalOperands << '\n';

    std::cout
        << "grammar operands    : "
        << grammarOperands
        << " ("
        << std::fixed
        << std::setprecision(2)
        << (100.0 * grammarOperands / totalOperands)
        << "%)\n";

    std::cout
        << "terminal operands   : "
        << (totalOperands - grammarOperands)
        << " ("
        << std::fixed
        << std::setprecision(2)
        << (100.0 *
            (totalOperands - grammarOperands) /
            totalOperands)
        << "%)\n";

    std::cout
        << "\nOperand type distribution\n"
        << "-------------------------\n";

    std::cout
        << "BB : "
        << bbOperands
        << " ("
        << std::fixed
        << std::setprecision(2)
        << (100.0 * bbOperands / totalOperands)
        << "%)\n";

    std::cout
        << "BG : "
        << bgOperands
        << " ("
        << std::fixed
        << std::setprecision(2)
        << (100.0 * bgOperands / totalOperands)
        << "%)\n";

    std::cout
        << "GB : "
        << gbOperands
        << " ("
        << std::fixed
        << std::setprecision(2)
        << (100.0 * gbOperands / totalOperands)
        << "%)\n";

    std::cout
        << "GG : "
        << ggOperands
        << " ("
        << std::fixed
        << std::setprecision(2)
        << (100.0 * ggOperands / totalOperands)
        << "%)\n";


    /*
     * TOP N
     */

    const uint32_t topLimits[] = {
        10,
        25,
        50,
        100,
        256,
        512,
        1024,
        2048
    };

    std::cout
        << "\nFrequency coverage\n"
        << "-------------------\n";

    std::cout
        << std::left
        << std::setw(8)  << "top"
        << std::setw(12) << "operands"
        << std::setw(12) << "coverage"
        << std::setw(12) << "grammar"
        << '\n';

    uint64_t cumulative = 0;
    uint32_t entryIndex = 0;

    for (uint32_t limit : topLimits) {
        while (entryIndex < entries.size() &&
               entryIndex < limit) {
            cumulative += entries[entryIndex].count;
            ++entryIndex;
        }

        uint64_t grammarCountTop = 0;

        for (uint32_t i = 0;
             i < entryIndex;
             ++i) {
            if (entries[i].token >= GTC_BASE_TOKENS)
                grammarCountTop += entries[i].count;
        }

        std::cout
            << std::left
            << std::setw(8)
            << limit
            << std::setw(12)
            << cumulative
            << std::setw(12)
            << std::fixed
            << std::setprecision(2)
            << (100.0 * cumulative / totalOperands)
            << "%"
            << std::setw(12)
            << std::fixed
            << std::setprecision(2)
            << (100.0 * grammarCountTop / totalOperands)
            << "%"
            << '\n';
    }


    /*
     * TOP 50
     */

    std::cout
        << "\nTop 50 operand IDs\n"
        << "------------------\n";

    std::cout
        << std::left
        << std::setw(6)  << "rank"
        << std::setw(8)  << "token"
        << std::setw(12) << "count"
        << std::setw(12) << "percent"
        << std::setw(12) << "cum%"
        << std::setw(10) << "type"
        << '\n';

    cumulative = 0;

    const size_t topCount =
        std::min<size_t>(50, entries.size());

    for (size_t i = 0; i < topCount; ++i) {
        const Entry& e = entries[i];

        cumulative += e.count;

        const char* type =
            e.token < GTC_BASE_TOKENS
                ? "BYTE"
                : "GRAMMAR";

        std::cout
            << std::left
            << std::setw(6)
            << (i + 1)
            << std::setw(8)
            << e.token
            << std::setw(12)
            << e.count
            << std::setw(12)
            << std::fixed
            << std::setprecision(2)
            << (100.0 * e.count / totalOperands)
            << "%"
            << std::setw(12)
            << std::fixed
            << std::setprecision(2)
            << (100.0 * cumulative / totalOperands)
            << "%"
            << std::setw(10)
            << type
            << '\n';
    }


    /*
     * GRAMMAR-ONLY ranking
     */

    std::vector<Entry> grammarEntries;
    grammarEntries.reserve(grammarCount);

    uint64_t grammarTotal = 0;

    for (uint32_t token = GTC_BASE_TOKENS;
         token < tokenCount;
         ++token) {

        if (totalFreq[token] != 0) {
            grammarEntries.push_back({
                token,
                totalFreq[token]
            });

            grammarTotal += totalFreq[token];
        }
    }

    std::sort(
        grammarEntries.begin(),
        grammarEntries.end(),
        [](const Entry& a, const Entry& b) {
            if (a.count != b.count)
                return a.count > b.count;

            return a.token < b.token;
        });

    std::cout
        << "\nGrammar-only frequency coverage\n"
        << "--------------------------------\n";

    const uint32_t grammarLimits[] = {
        10,
        25,
        50,
        100,
        256,
        512,
        1024,
        2048,
        4096
    };

    std::cout
        << std::left
        << std::setw(8)  << "top"
        << std::setw(12) << "references"
        << std::setw(12) << "coverage"
        << '\n';

    cumulative = 0;
    entryIndex = 0;

    for (uint32_t limit : grammarLimits) {
        while (entryIndex < grammarEntries.size() &&
               entryIndex < limit) {

            cumulative +=
                grammarEntries[entryIndex].count;

            ++entryIndex;
        }

        std::cout
            << std::left
            << std::setw(8)
            << limit
            << std::setw(12)
            << cumulative
            << std::setw(12)
            << std::fixed
            << std::setprecision(2)
            << (100.0 * cumulative / grammarTotal)
            << "%"
            << '\n';
    }


    /*
     * Grammar-only top 50.
     */

    std::cout
        << "\nTop 50 grammar operand IDs\n"
        << "--------------------------\n";

    std::cout
        << std::left
        << std::setw(6)  << "rank"
        << std::setw(8)  << "token"
        << std::setw(12) << "count"
        << std::setw(12) << "percent"
        << std::setw(12) << "cum%"
        << '\n';

    cumulative = 0;

    const size_t grammarTopCount =
        std::min<size_t>(
            50,
            grammarEntries.size());

    for (size_t i = 0;
         i < grammarTopCount;
         ++i) {

        const Entry& e =
            grammarEntries[i];

        cumulative += e.count;

        std::cout
            << std::left
            << std::setw(6)
            << (i + 1)
            << std::setw(8)
            << e.token
            << std::setw(12)
            << e.count
            << std::setw(12)
            << std::fixed
            << std::setprecision(2)
            << (100.0 * e.count / grammarTotal)
            << "%"
            << std::setw(12)
            << std::fixed
            << std::setprecision(2)
            << (100.0 * cumulative / grammarTotal)
            << "%"
            << '\n';
    }


    /*
     * Left/right separately.
     */

    auto printSide =
        [](const char* name,
           const std::vector<uint64_t>& freq,
           uint64_t total) {

        std::vector<Entry> side;

        for (uint32_t token = 0;
             token < freq.size();
             ++token) {

            if (freq[token] != 0) {
                side.push_back({
                    token,
                    freq[token]
                });
            }
        }

        std::sort(
            side.begin(),
            side.end(),
            [](const Entry& a, const Entry& b) {
                if (a.count != b.count)
                    return a.count > b.count;

                return a.token < b.token;
            });

        std::cout
            << "\nTop 20 "
            << name
            << " operands\n"
            << "----------------------\n";

        std::cout
            << std::left
            << std::setw(6)  << "rank"
            << std::setw(8)  << "token"
            << std::setw(12) << "count"
            << std::setw(12) << "percent"
            << '\n';

        const size_t count =
            std::min<size_t>(20, side.size());

        for (size_t i = 0; i < count; ++i) {
            std::cout
                << std::left
                << std::setw(6)
                << (i + 1)
                << std::setw(8)
                << side[i].token
                << std::setw(12)
                << side[i].count
                << std::setw(12)
                << std::fixed
                << std::setprecision(2)
                << (100.0 * side[i].count / total)
                << "%"
                << '\n';
        }
    };

    printSide(
        "left",
        leftFreq,
        totalOperands / 2);

    printSide(
        "right",
        rightFreq,
        totalOperands / 2);
}

static void analyzeOperandDifference(
    const GtcDictionary& dictionary)
{
    const uint32_t ruleCount = dictionary.size();

    /*
     * Exact histogram for small differences.
     * 0..64 are shown individually.
     */
    uint64_t small[65] = {};

    /*
     * Logarithmic buckets:
     *
     * 0
     * 1
     * 2..3
     * 4..7
     * 8..15
     * ...
     * 4096..8191
     */
    uint64_t buckets[16] = {};

    /*
     * Statistics by rule category:
     *
     * BB = byte -> byte
     * BG = byte -> grammar
     * GB = grammar -> byte
     * GG = grammar -> grammar
     */
    uint64_t typeCount[4] = {};
    uint64_t typeSum[4] = {};
    uint32_t typeMin[4] = {};
    uint32_t typeMax[4] = {};

    uint64_t totalCount = 0;
    uint64_t totalSum = 0;

    uint32_t globalMin = UINT32_MAX;
    uint32_t globalMax = 0;

    /*
     * Small-distance coverage.
     */
    uint64_t le1 = 0;
    uint64_t le2 = 0;
    uint64_t le4 = 0;
    uint64_t le8 = 0;
    uint64_t le16 = 0;
    uint64_t le32 = 0;
    uint64_t le64 = 0;
    uint64_t le128 = 0;
    uint64_t le256 = 0;
    uint64_t le512 = 0;
    uint64_t le1024 = 0;

    for (uint32_t i = 0; i < ruleCount; ++i) {

        const uint32_t token =
            GTC_BASE_TOKENS + i;

        const GtcRule& rule =
            dictionary.rule(token);

        const uint32_t left =
            static_cast<uint32_t>(rule.left);

        const uint32_t right =
            static_cast<uint32_t>(rule.right);

        const uint32_t difference =
            (left >= right)
                ? (left - right)
                : (right - left);

        const bool leftGrammar =
            left >= GTC_BASE_TOKENS;

        const bool rightGrammar =
            right >= GTC_BASE_TOKENS;

        uint32_t type;

        if (!leftGrammar && !rightGrammar)
            type = 0; // BB
        else if (!leftGrammar && rightGrammar)
            type = 1; // BG
        else if (leftGrammar && !rightGrammar)
            type = 2; // GB
        else
            type = 3; // GG

        ++totalCount;
        totalSum += difference;

        if (difference < globalMin)
            globalMin = difference;

        if (difference > globalMax)
            globalMax = difference;

        ++typeCount[type];
        typeSum[type] += difference;

        if (typeMin[type] == 0 ||
            difference < typeMin[type])
            typeMin[type] = difference;

        if (difference > typeMax[type])
            typeMax[type] = difference;

        /*
         * Exact small-value histogram.
         */
        if (difference <= 64)
            ++small[difference];

        /*
         * Coverage.
         */
        if (difference <= 1)
            ++le1;

        if (difference <= 2)
            ++le2;

        if (difference <= 4)
            ++le4;

        if (difference <= 8)
            ++le8;

        if (difference <= 16)
            ++le16;

        if (difference <= 32)
            ++le32;

        if (difference <= 64)
            ++le64;

        if (difference <= 128)
            ++le128;

        if (difference <= 256)
            ++le256;

        if (difference <= 512)
            ++le512;

        if (difference <= 1024)
            ++le1024;

        /*
         * Logarithmic bucket.
         */
        uint32_t bucket = 0;

        if (difference == 0) {
            bucket = 0;
        } else {
            uint32_t value = difference;

            /*
             * 1       -> bucket 1
             * 2..3    -> bucket 2
             * 4..7    -> bucket 3
             * ...
             */
            bucket = 1;

            while (value > 1 && bucket < 15) {
                value >>= 1;
                ++bucket;
            }
        }

        ++buckets[bucket];
    }

    const char* typeNames[4] = {
        "BB",
        "BG",
        "GB",
        "GG"
    };

    std::printf("\n");
    std::printf(
        "Operand absolute-difference analysis\n");
    std::printf(
        "------------------------------------\n");

    std::printf(
        "rules              : %u\n",
        ruleCount);

    std::printf(
        "min difference     : %u\n",
        globalMin);

    std::printf(
        "max difference     : %u\n",
        globalMax);

    std::printf(
        "average difference : %.2f\n",
        totalCount != 0
            ? static_cast<double>(totalSum) /
              static_cast<double>(totalCount)
            : 0.0);

    /*
     * Coverage.
     */
    std::printf("\n");
    std::printf(
        "Small-difference coverage\n");
    std::printf(
        "-------------------------\n");

    const struct {
        uint32_t limit;
        uint64_t count;
    } coverage[] = {
        {   1, le1     },
        {   2, le2     },
        {   4, le4     },
        {   8, le8     },
        {  16, le16   },
        {  32, le32   },
        {  64, le64   },
        { 128, le128  },
        { 256, le256  },
        { 512, le512  },
        {1024, le1024 }
    };

    for (const auto& item : coverage) {

        const double percent =
            totalCount != 0
                ? 100.0 *
                  static_cast<double>(
                      item.count) /
                  static_cast<double>(
                      totalCount)
                : 0.0;

        std::printf(
            "<= %-4u : %6llu (%.2f%%)\n",
            item.limit,
            static_cast<unsigned long long>(
                item.count),
            percent);
    }

    /*
     * Exact histogram 0..64.
     */
    std::printf("\n");
    std::printf(
        "Exact difference histogram (0..64)\n");
    std::printf(
        "----------------------------------\n");

    std::printf(
        "%10s %12s %10s\n",
        "difference",
        "rules",
        "percent");

    for (uint32_t d = 0; d <= 64; ++d) {

        if (small[d] == 0)
            continue;

        const double percent =
            100.0 *
            static_cast<double>(
                small[d]) /
            static_cast<double>(
                totalCount);

        std::printf(
            "%10u %12llu %9.2f%%\n",
            d,
            static_cast<unsigned long long>(
                small[d]),
            percent);
    }

    /*
     * Logarithmic buckets.
     */
    std::printf("\n");
    std::printf(
        "Difference buckets\n");
    std::printf(
        "------------------\n");

    std::printf(
        "%-18s %12s %10s\n",
        "difference",
        "rules",
        "percent");

    for (uint32_t b = 0; b < 16; ++b) {

        char label[32];

        if (b == 0) {

            std::snprintf(
                label,
                sizeof(label),
                "0");

        } else if (b == 15) {

            const uint32_t low =
                1u << 14;

            std::snprintf(
                label,
                sizeof(label),
                "%u+",
                low);

        } else {

            const uint32_t low =
                1u << (b - 1);

            const uint32_t high =
                (1u << b) - 1u;

            std::snprintf(
                label,
                sizeof(label),
                "%u..%u",
                low,
                high);
        }

        if (buckets[b] == 0)
            continue;

        const double percent =
            100.0 *
            static_cast<double>(
                buckets[b]) /
            static_cast<double>(
                totalCount);

        std::printf(
            "%-18s %12llu %9.2f%%\n",
            label,
            static_cast<unsigned long long>(
                buckets[b]),
            percent);
    }

    /*
     * Statistics by rule category.
     */
    std::printf("\n");
    std::printf(
        "Difference by rule category\n");
    std::printf(
        "----------------------------\n");

    std::printf(
        "%-4s %10s %12s %12s %12s\n",
        "type",
        "rules",
        "avg diff",
        "min",
        "max");

    for (uint32_t type = 0; type < 4; ++type) {

        const double average =
            typeCount[type] != 0
                ? static_cast<double>(
                      typeSum[type]) /
                  static_cast<double>(
                      typeCount[type])
                : 0.0;

        std::printf(
            "%-4s %10llu %12.2f %12u %12u\n",
            typeNames[type],

            static_cast<unsigned long long>(
                typeCount[type]),

            average,

            typeCount[type] != 0
                ? typeMin[type]
                : 0,

            typeCount[type] != 0
                ? typeMax[type]
                : 0);
    }
}
static void analyzeOperandDistances(
    const GtcDictionary& dictionary)
{
    const uint32_t ruleCount = dictionary.size();

    /*
     * Fine histogram for small distances.
     *
     * 1..64 are shown individually.
     * Everything larger goes into buckets.
     */
    uint64_t leftSmall[65] = {};
    uint64_t rightSmall[65] = {};

    uint64_t leftBuckets[16] = {};
    uint64_t rightBuckets[16] = {};

    /*
     * Separate statistics by operand type:
     *
     * BB = byte -> byte
     * BG = byte -> grammar
     * GB = grammar -> byte
     * GG = grammar -> grammar
     *
     * For distance analysis only grammar operands have
     * a meaningful backward reference.
     */
    uint64_t leftType[4] = {};
    uint64_t rightType[4] = {};

    uint64_t leftDistanceSum[4] = {};
    uint64_t rightDistanceSum[4] = {};

    uint32_t leftMin[4] = {};
    uint32_t rightMin[4] = {};

    uint32_t leftMax[4] = {};
    uint32_t rightMax[4] = {};

    /*
     * Overall distance statistics.
     */
    uint64_t leftGrammarRefs = 0;
    uint64_t rightGrammarRefs = 0;

    uint64_t leftDistanceSumAll = 0;
    uint64_t rightDistanceSumAll = 0;

    uint32_t leftMinAll = UINT32_MAX;
    uint32_t rightMinAll = UINT32_MAX;

    uint32_t leftMaxAll = 0;
    uint32_t rightMaxAll = 0;

    /*
     * Count very small distances.
     */
    uint64_t leftDistanceLE8 = 0;
    uint64_t leftDistanceLE16 = 0;
    uint64_t leftDistanceLE32 = 0;
    uint64_t leftDistanceLE64 = 0;

    uint64_t rightDistanceLE8 = 0;
    uint64_t rightDistanceLE16 = 0;
    uint64_t rightDistanceLE32 = 0;
    uint64_t rightDistanceLE64 = 0;

    for (uint32_t i = 0; i < ruleCount; ++i) {

        const uint32_t token =
            GTC_BASE_TOKENS + i;

        const GtcRule& rule =
            dictionary.rule(token);

        const bool leftGrammar =
            rule.left >= GTC_BASE_TOKENS;

        const bool rightGrammar =
            rule.right >= GTC_BASE_TOKENS;

        uint32_t type;

        if (!leftGrammar && !rightGrammar)
            type = 0; // BB
        else if (!leftGrammar && rightGrammar)
            type = 1; // BG
        else if (leftGrammar && !rightGrammar)
            type = 2; // GB
        else
            type = 3; // GG

        /*
         * LEFT operand
         */
        if (leftGrammar) {

            /*
             * Rules can only reference tokens which already
             * existed before the current rule.
             */
            if (rule.left >= token) {
                std::printf(
                    "WARNING: invalid left reference "
                    "T%u -> T%u\n",
                    token,
                    static_cast<unsigned>(rule.left));

                continue;
            }

            const uint32_t distance =
                token - rule.left;

            ++leftGrammarRefs;
            leftDistanceSumAll += distance;

            if (distance < leftMinAll)
                leftMinAll = distance;

            if (distance > leftMaxAll)
                leftMaxAll = distance;

            ++leftType[type];
            leftDistanceSum[type] += distance;

            if (leftMin[type] == 0 ||
                distance < leftMin[type])
                leftMin[type] = distance;

            if (distance > leftMax[type])
                leftMax[type] = distance;

            if (distance <= 64)
                ++leftSmall[distance];

            if (distance <= 8)
                ++leftDistanceLE8;

            if (distance <= 16)
                ++leftDistanceLE16;

            if (distance <= 32)
                ++leftDistanceLE32;

            if (distance <= 64)
                ++leftDistanceLE64;

            /*
             * Log2-like buckets:
             *
             * 1
             * 2..3
             * 4..7
             * 8..15
             * ...
             */
            uint32_t bucket = 0;
            uint32_t value = distance;

            while (value > 1 && bucket < 15) {
                value >>= 1;
                ++bucket;
            }

            ++leftBuckets[bucket];
        }

        /*
         * RIGHT operand
         */
        if (rightGrammar) {

            if (rule.right >= token) {
                std::printf(
                    "WARNING: invalid right reference "
                    "T%u -> T%u\n",
                    token,
                    static_cast<unsigned>(rule.right));

                continue;
            }

            const uint32_t distance =
                token - rule.right;

            ++rightGrammarRefs;
            rightDistanceSumAll += distance;

            if (distance < rightMinAll)
                rightMinAll = distance;

            if (distance > rightMaxAll)
                rightMaxAll = distance;

            ++rightType[type];
            rightDistanceSum[type] += distance;

            if (rightMin[type] == 0 ||
                distance < rightMin[type])
                rightMin[type] = distance;

            if (distance > rightMax[type])
                rightMax[type] = distance;

            if (distance <= 64)
                ++rightSmall[distance];

            if (distance <= 8)
                ++rightDistanceLE8;

            if (distance <= 16)
                ++rightDistanceLE16;

            if (distance <= 32)
                ++rightDistanceLE32;

            if (distance <= 64)
                ++rightDistanceLE64;

            uint32_t bucket = 0;
            uint32_t value = distance;

            while (value > 1 && bucket < 15) {
                value >>= 1;
                ++bucket;
            }

            ++rightBuckets[bucket];
        }
    }

    const char* typeNames[4] = {
        "BB",
        "BG",
        "GB",
        "GG"
    };

    std::printf("\n");
    std::printf(
        "Operand backward-distance analysis\n");
    std::printf(
        "----------------------------------\n");

    std::printf(
        "rules              : %u\n",
        ruleCount);

    std::printf(
        "left grammar refs  : %llu\n",
        static_cast<unsigned long long>(
            leftGrammarRefs));

    std::printf(
        "right grammar refs : %llu\n",
        static_cast<unsigned long long>(
            rightGrammarRefs));

    /*
     * Overall statistics.
     */
    std::printf("\n");
    std::printf(
        "Overall distance statistics\n");
    std::printf(
        "---------------------------\n");

    if (leftGrammarRefs != 0) {
        std::printf(
            "left  min / max / avg : %u / %u / %.2f\n",
            leftMinAll,
            leftMaxAll,
            static_cast<double>(
                leftDistanceSumAll) /
            static_cast<double>(
                leftGrammarRefs));
    }

    if (rightGrammarRefs != 0) {
        std::printf(
            "right min / max / avg : %u / %u / %.2f\n",
            rightMinAll,
            rightMaxAll,
            static_cast<double>(
                rightDistanceSumAll) /
            static_cast<double>(
                rightGrammarRefs));
    }

    /*
     * Small-distance coverage.
     */
    std::printf("\n");
    std::printf(
        "Small-distance coverage\n");
    std::printf(
        "-----------------------\n");

    if (leftGrammarRefs != 0) {
        std::printf(
            "left  <= 8   : %llu (%.2f%%)\n",
            static_cast<unsigned long long>(
                leftDistanceLE8),
            100.0 *
            static_cast<double>(
                leftDistanceLE8) /
            static_cast<double>(
                leftGrammarRefs));

        std::printf(
            "left  <= 16  : %llu (%.2f%%)\n",
            static_cast<unsigned long long>(
                leftDistanceLE16),
            100.0 *
            static_cast<double>(
                leftDistanceLE16) /
            static_cast<double>(
                leftGrammarRefs));

        std::printf(
            "left  <= 32  : %llu (%.2f%%)\n",
            static_cast<unsigned long long>(
                leftDistanceLE32),
            100.0 *
            static_cast<double>(
                leftDistanceLE32) /
            static_cast<double>(
                leftGrammarRefs));

        std::printf(
            "left  <= 64  : %llu (%.2f%%)\n",
            static_cast<unsigned long long>(
                leftDistanceLE64),
            100.0 *
            static_cast<double>(
                leftDistanceLE64) /
            static_cast<double>(
                leftGrammarRefs));
    }

    if (rightGrammarRefs != 0) {
        std::printf(
            "right <= 8   : %llu (%.2f%%)\n",
            static_cast<unsigned long long>(
                rightDistanceLE8),
            100.0 *
            static_cast<double>(
                rightDistanceLE8) /
            static_cast<double>(
                rightGrammarRefs));

        std::printf(
            "right <= 16  : %llu (%.2f%%)\n",
            static_cast<unsigned long long>(
                rightDistanceLE16),
            100.0 *
            static_cast<double>(
                rightDistanceLE16) /
            static_cast<double>(
                rightGrammarRefs));

        std::printf(
            "right <= 32  : %llu (%.2f%%)\n",
            static_cast<unsigned long long>(
                rightDistanceLE32),
            100.0 *
            static_cast<double>(
                rightDistanceLE32) /
            static_cast<double>(
                rightGrammarRefs));

        std::printf(
            "right <= 64  : %llu (%.2f%%)\n",
            static_cast<unsigned long long>(
                rightDistanceLE64),
            100.0 *
            static_cast<double>(
                rightDistanceLE64) /
            static_cast<double>(
                rightGrammarRefs));
    }

    /*
     * Exact histogram 1..64.
     */
    std::printf("\n");
    std::printf(
        "Exact distance histogram (1..64)\n");
    std::printf(
        "---------------------------------\n");

    std::printf(
        "%8s %12s %12s\n",
        "distance",
        "left",
        "right");

    for (uint32_t d = 1; d <= 64; ++d) {

        if (leftSmall[d] == 0 &&
            rightSmall[d] == 0)
            continue;

        std::printf(
            "%8u %12llu %12llu\n",
            d,
            static_cast<unsigned long long>(
                leftSmall[d]),
            static_cast<unsigned long long>(
                rightSmall[d]));
    }

    /*
     * Logarithmic buckets.
     */
    std::printf("\n");
    std::printf(
        "Distance buckets\n");
    std::printf(
        "----------------\n");

    std::printf(
        "%-16s %12s %12s\n",
        "distance",
        "left",
        "right");

for (uint32_t b = 0; b < 16; ++b) {

    uint32_t low;
    uint32_t high;

    if (b == 0) {
        low = 1;
        high = 1;
    } else {
        low = 1u << b;
        high = (1u << (b + 1)) - 1u;
    }

    char label[32];

    if (b == 0) {
        std::snprintf(label, sizeof(label), "1");
    } else if (b == 15) {
        std::snprintf(
            label,
            sizeof(label),
            "%u+",
            low);
    } else {
        std::snprintf(
            label,
            sizeof(label),
            "%u..%u",
            low,
            high);
    }

    std::printf(
        "%-16s %12llu %12llu\n",
        label,
        static_cast<unsigned long long>(
            leftBuckets[b]),
        static_cast<unsigned long long>(
            rightBuckets[b]));
}

    /*
     * By rule category.
     */
    std::printf("\n");
    std::printf(
        "Distance by rule category\n");
    std::printf(
        "-------------------------\n");

    std::printf(
        "%-4s %10s %12s %12s "
        "%10s %12s %12s %10s\n",
        "type",
        "left refs",
        "left avg",
        "left max",
        "right refs",
        "right avg",
        "right max",
        "");

    for (uint32_t type = 0; type < 4; ++type) {

        double leftAvg = 0.0;
        double rightAvg = 0.0;

        if (leftType[type] != 0) {
            leftAvg =
                static_cast<double>(
                    leftDistanceSum[type]) /
                static_cast<double>(
                    leftType[type]);
        }

        if (rightType[type] != 0) {
            rightAvg =
                static_cast<double>(
                    rightDistanceSum[type]) /
                static_cast<double>(
                    rightType[type]);
        }

        std::printf(
            "%-4s %10llu %12.2f %12u "
            "%10llu %12.2f %12u\n",
            typeNames[type],

            static_cast<unsigned long long>(
                leftType[type]),
            leftAvg,
            leftMax[type],

            static_cast<unsigned long long>(
                rightType[type]),
            rightAvg,
            rightMax[type]);
    }
}
static void analyzeOperandWidthWaste(
    const GtcDictionary& dictionary)
{
    const uint32_t ruleCount = dictionary.size();

    uint64_t totalActualBits = 0;
    uint64_t totalCommonBits = 0;

    uint64_t categoryActual[4] = {};
    uint64_t categoryCommon[4] = {};
    uint64_t categoryWaste[4] = {};
    uint64_t categoryRules[4] = {};

    /*
     * wasteHistogram[n] =
     * number of rules where common-width encoding
     * wastes exactly n bits compared with independent
     * operand widths.
     */
    uint64_t wasteHistogram[32] = {};

    uint32_t maxWaste = 0;

    uint32_t maxWasteToken = 0;
    uint32_t maxWasteLeft = 0;
    uint32_t maxWasteRight = 0;

    uint64_t maxWasteCount = 0;

    /*
     * How much total waste comes from each exact
     * (leftBits, rightBits) pair.
     */
    uint64_t pairWaste[14][14] = {};

    for (uint32_t i = 0; i < ruleCount; ++i) {
        const uint32_t token =
            GTC_BASE_TOKENS + i;

        const GtcRule& rule =
            dictionary.rule(token);

        const uint32_t leftBits =
            bitsForValue(rule.left);

        const uint32_t rightBits =
            bitsForValue(rule.right);

        const uint32_t commonWidth =
            (leftBits > rightBits)
                ? leftBits
                : rightBits;

        const uint32_t actualBits =
            leftBits + rightBits;

        const uint32_t commonBits =
            commonWidth * 2;

        const uint32_t waste =
            commonBits - actualBits;

        totalActualBits += actualBits;
        totalCommonBits += commonBits;

        /*
         * Classify BB / BG / GB / GG.
         */
        const bool leftGrammar =
            rule.left >= GTC_BASE_TOKENS;

        const bool rightGrammar =
            rule.right >= GTC_BASE_TOKENS;

        uint32_t category;

        if (!leftGrammar && !rightGrammar)
            category = 0; // BB
        else if (!leftGrammar && rightGrammar)
            category = 1; // BG
        else if (leftGrammar && !rightGrammar)
            category = 2; // GB
        else
            category = 3; // GG

        ++categoryRules[category];
        categoryActual[category] += actualBits;
        categoryCommon[category] += commonBits;
        categoryWaste[category] += waste;

        if (waste < 32)
            ++wasteHistogram[waste];

        pairWaste[leftBits][rightBits] += waste;

        if (waste > maxWaste) {
            maxWaste = waste;
            maxWasteToken = token;
            maxWasteLeft = leftBits;
            maxWasteRight = rightBits;
            maxWasteCount = 1;
        } else if (waste == maxWaste) {
            ++maxWasteCount;
        }
    }

    const uint64_t totalWaste =
        totalCommonBits - totalActualBits;

    std::printf("\n");
    std::printf(
        "Operand width waste analysis\n");
    std::printf(
        "----------------------------\n");

    std::printf(
        "rules                 : %u\n",
        ruleCount);

    std::printf(
        "actual bits           : %llu\n",
        static_cast<unsigned long long>(
            totalActualBits));

    std::printf(
        "common-width bits     : %llu\n",
        static_cast<unsigned long long>(
            totalCommonBits));

    std::printf(
        "wasted bits            : %llu\n",
        static_cast<unsigned long long>(
            totalWaste));

    std::printf(
        "wasted bytes           : %llu\n",
        static_cast<unsigned long long>(
            (totalWaste + 7) / 8));

    std::printf(
        "max waste per rule     : %u bits\n",
        maxWaste);

    std::printf(
        "rules at max waste     : %llu\n",
        static_cast<unsigned long long>(
            maxWasteCount));

    std::printf(
        "example max-waste rule : T%u (%u + %u bits)\n",
        maxWasteToken,
        maxWasteLeft,
        maxWasteRight);

    /*
     * Waste histogram.
     */
    std::printf("\n");
    std::printf(
        "Waste histogram\n");
    std::printf(
        "---------------\n");
    std::printf(
        "waste bits     rules       total bits\n");

    for (uint32_t waste = 0;
         waste < 32;
         ++waste) {

        if (wasteHistogram[waste] == 0)
            continue;

        std::printf(
            "%10u %10llu %15llu\n",
            waste,
            static_cast<unsigned long long>(
                wasteHistogram[waste]),
            static_cast<unsigned long long>(
                wasteHistogram[waste] *
                uint64_t(waste)));
    }

    /*
     * Category statistics.
     */
    std::printf("\n");
    std::printf(
        "Waste by rule category\n");
    std::printf(
        "----------------------\n");

    std::printf(
        "%-4s %8s %12s %12s %12s %12s\n",
        "type",
        "rules",
        "actual",
        "common",
        "waste",
        "avg waste");

    const char* names[4] = {
        "BB",
        "BG",
        "GB",
        "GG"
    };

    for (uint32_t c = 0; c < 4; ++c) {
        if (categoryRules[c] == 0)
            continue;

        const double avgWaste =
            static_cast<double>(
                categoryWaste[c]) /
            static_cast<double>(
                categoryRules[c]);

        std::printf(
            "%-4s %8llu %12llu %12llu %12llu %11.3f\n",
            names[c],
            static_cast<unsigned long long>(
                categoryRules[c]),
            static_cast<unsigned long long>(
                categoryActual[c]),
            static_cast<unsigned long long>(
                categoryCommon[c]),
            static_cast<unsigned long long>(
                categoryWaste[c]),
            avgWaste);
    }

    /*
     * Waste grouped by operand-width pair.
     */
    std::printf("\n");
    std::printf(
        "Waste by operand-width pair\n");
    std::printf(
        "---------------------------\n");

    std::printf(
        "%-10s %12s %15s\n",
        "widths",
        "rules",
        "wasted bits");

    for (uint32_t left = 1;
         left < 14;
         ++left) {

        for (uint32_t right = 1;
             right < 14;
             ++right) {

            uint64_t waste =
                pairWaste[left][right];

            if (waste == 0)
                continue;

            /*
             * Recover number of rules for this pair
             * from the previously available histogram.
             *
             * Instead of maintaining another matrix,
             * print only the total wasted bits here.
             */
            std::printf(
                "%2u + %-5u %12s %15llu\n",
                left,
                right,
                "-",
                static_cast<unsigned long long>(
                    waste));
        }
    }

    /*
     * Final comparison.
     */
    const uint64_t actualBytes =
        (totalActualBits + 7) / 8;

    const uint64_t commonBytes =
        (totalCommonBits + 7) / 8;

    std::printf("\n");
    std::printf(
        "Packed size comparison\n");
    std::printf(
        "----------------------\n");

    std::printf(
        "independent widths : %llu bytes\n",
        static_cast<unsigned long long>(
            actualBytes));

    std::printf(
        "common width       : %llu bytes\n",
        static_cast<unsigned long long>(
            commonBytes));

    if (commonBytes >= actualBytes) {
        std::printf(
            "difference         : %llu bytes\n",
            static_cast<unsigned long long>(
                commonBytes - actualBytes));
    }
}
static void analyzeOperandBitPairs(
    const GtcDictionary& dictionary)
{
    const uint32_t ruleCount = dictionary.size();

    uint64_t pairCount[14][14] = {};
    uint64_t totalBits = 0;

    uint32_t minLeft = 32;
    uint32_t maxLeft = 0;
    uint32_t minRight = 32;
    uint32_t maxRight = 0;

    for (uint32_t i = 0; i < ruleCount; ++i) {
        const uint32_t token =
            GTC_BASE_TOKENS + i;

        const GtcRule& rule =
            dictionary.rule(token);

        const uint32_t leftBits =
            bitsForValue(rule.left);

        const uint32_t rightBits =
            bitsForValue(rule.right);

        ++pairCount[leftBits][rightBits];

        totalBits +=
            uint64_t(leftBits) +
            uint64_t(rightBits);

        if (leftBits < minLeft)
            minLeft = leftBits;

        if (leftBits > maxLeft)
            maxLeft = leftBits;

        if (rightBits < minRight)
            minRight = rightBits;

        if (rightBits > maxRight)
            maxRight = rightBits;
    }

    std::printf("\n");
    std::printf(
        "Operand bit-width pairs\n");
    std::printf(
        "-----------------------\n");

    std::printf(
        "left/right");

    for (uint32_t right = minRight;
         right <= maxRight;
         ++right) {
        std::printf(
            "%10u",
            right);
    }

    std::printf("\n");

    for (uint32_t left = minLeft;
         left <= maxLeft;
         ++left) {

        bool used = false;

        for (uint32_t right = minRight;
             right <= maxRight;
             ++right) {
            if (pairCount[left][right] != 0) {
                used = true;
                break;
            }
        }

        if (!used)
            continue;

        std::printf(
            "%4u      ",
            left);

        for (uint32_t right = minRight;
             right <= maxRight;
             ++right) {
            if (pairCount[left][right] == 0) {
                std::printf(
                    "%10s",
                    ".");
            } else {
                std::printf(
                    "%10llu",
                    static_cast<unsigned long long>(
                        pairCount[left][right]));
            }
        }

        std::printf("\n");
    }

    uint32_t usedPairs = 0;

    for (uint32_t left = 1;
         left < 14;
         ++left) {
        for (uint32_t right = 1;
             right < 14;
             ++right) {
            if (pairCount[left][right] != 0)
                ++usedPairs;
        }
    }

    std::printf("\n");
    std::printf(
        "Distinct width pairs : %u\n",
        usedPairs);

    std::printf(
        "Total operand bits    : %llu\n",
        static_cast<unsigned long long>(
            totalBits));

    std::printf(
        "Theoretical bytes     : %llu\n",
        static_cast<unsigned long long>(
            (totalBits + 7) / 8));

    std::printf("\n");
    std::printf(
        "Width pair list\n");
    std::printf(
        "---------------\n");

    for (uint32_t left = 1;
         left < 14;
         ++left) {

        for (uint32_t right = 1;
             right < 14;
             ++right) {

            if (pairCount[left][right] == 0)
                continue;

            std::printf(
                "%2u + %2u : %llu\n",
                left,
                right,
                static_cast<unsigned long long>(
                    pairCount[left][right]));
        }
    }
}

static void analyzeDynamicGrammarBits(
    const GtcDictionary& dictionary)
{
    const uint32_t ruleCount = dictionary.size();

    uint64_t totalBits = 0;
    uint64_t widthRules[32] = {};

    for (uint32_t i = 0; i < ruleCount; ++i) {
        const uint32_t token =
            GTC_BASE_TOKENS + i;

        /*
         * Before creating Tn, all tokens below Tn
         * already exist.
         *
         * Base tokens: 0..255
         * First grammar token: 256
         */
        const uint32_t maxAvailable =
            token - 1;

        const uint32_t width =
            bitsForToken(maxAvailable);

        const GtcRule& rule =
            dictionary.rule(token);

        /*
         * Verify that both operands fit into the width
         * available when this rule was created.
         */
        if (bitsForToken(rule.left) > width ||
            bitsForToken(rule.right) > width) {
            std::printf(
                "ERROR: rule %u does not fit into %u bits: "
                "%u + %u\n",
                token,
                width,
                rule.left,
                rule.right);
        }

        totalBits +=
            uint64_t(width) * 2;

        ++widthRules[width];
    }

    const uint64_t totalBytes =
        (totalBits + 7) / 8;

    std::printf("\n");
    std::printf(
        "Dynamic grammar bit width\n");
    std::printf(
        "-------------------------\n");
    std::printf(
        "width       rules        bits\n");

    for (uint32_t width = 1;
         width < 32;
         ++width) {

        if (widthRules[width] == 0)
            continue;

        std::printf(
            "%5u %11llu %12llu\n",
            width,
            static_cast<unsigned long long>(
                widthRules[width]),
            static_cast<unsigned long long>(
                widthRules[width] *
                uint64_t(width) *
                2));
    }

    std::printf("\n");

    std::printf(
        "dynamic grammar bits  : %llu\n",
        static_cast<unsigned long long>(
            totalBits));

    std::printf(
        "dynamic grammar bytes : %llu\n",
        static_cast<unsigned long long>(
            totalBytes));

    std::printf(
        "uint16 grammar bytes  : %llu\n",
        static_cast<unsigned long long>(
            uint64_t(ruleCount) * 4));

    if (uint64_t(ruleCount) * 4 >= totalBytes) {
        std::printf(
            "saving                : %llu bytes\n",
            static_cast<unsigned long long>(
                uint64_t(ruleCount) * 4 -
                totalBytes));
    }
}
static void analyzeOperandBitWidths(
    const GtcDictionary& dictionary)
{
    const uint32_t ruleCount = dictionary.size();

    uint64_t actualLeftBits = 0;
    uint64_t actualRightBits = 0;

    uint64_t commonWidthBits = 0;
    uint64_t pairLowerBoundBits = 0;

    /*
     * Statistics by:
     *
     *   0 = BB
     *   1 = BG
     *   2 = GB
     *   3 = GG
     */
    uint64_t categoryCount[4] = {};
    uint64_t categoryLeftBits[4] = {};
    uint64_t categoryRightBits[4] = {};
    uint64_t categoryActualBits[4] = {};
    uint64_t categoryCommonBits[4] = {};
    uint64_t categoryPairBits[4] = {};

    uint32_t maxLeftBits = 0;
    uint32_t maxRightBits = 0;

    for (uint32_t i = 0; i < ruleCount; ++i) {
        const uint32_t token =
            GTC_BASE_TOKENS + i;

        const GtcRule& rule =
            dictionary.rule(token);

        const uint32_t leftBits =
            bitsForValue(rule.left);

        const uint32_t rightBits =
            bitsForValue(rule.right);

        /*
         * Actual independent operand widths.
         */
        actualLeftBits += leftBits;
        actualRightBits += rightBits;

        /*
         * Width that would be needed if both operands
         * use the same number of bits.
         *
         * This is exactly the dynamic-width scheme
         * measured previously.
         */
        const uint32_t commonWidth =
            (leftBits > rightBits)
                ? leftBits
                : rightBits;

        commonWidthBits +=
            uint64_t(commonWidth) * 2;

        /*
         * Classify the rule.
         */
        const bool leftGrammar =
            rule.left >= GTC_BASE_TOKENS;

        const bool rightGrammar =
            rule.right >= GTC_BASE_TOKENS;

        uint32_t category;

        if (!leftGrammar && !rightGrammar)
            category = 0; // BB
        else if (!leftGrammar && rightGrammar)
            category = 1; // BG
        else if (leftGrammar && !rightGrammar)
            category = 2; // GB
        else
            category = 3; // GG

        ++categoryCount[category];

        categoryLeftBits[category] += leftBits;
        categoryRightBits[category] += rightBits;

        categoryActualBits[category] +=
            uint64_t(leftBits) +
            uint64_t(rightBits);

        categoryCommonBits[category] +=
            uint64_t(commonWidth) * 2;

        if (leftBits > maxLeftBits)
            maxLeftBits = leftBits;

        if (rightBits > maxRightBits)
            maxRightBits = rightBits;

        /*
         * Theoretical pair lower bound.
         *
         * For a BB rule there are at most 256*256
         * possible pairs.
         *
         * For mixed/grammar pairs we use the actual
         * number of possible values at the point where
         * the rule was created.
         *
         * This is only a lower-bound estimate:
         * it does not include any signalling overhead.
         */
        uint32_t leftValues;
        uint32_t rightValues;

        if (leftGrammar) {
            /*
             * Before T(token) is created, grammar tokens
             * available are 256 .. token-1.
             */
            leftValues =
                token - GTC_BASE_TOKENS;
        } else {
            leftValues = GTC_BASE_TOKENS;
        }

        if (rightGrammar) {
            rightValues =
                token - GTC_BASE_TOKENS;
        } else {
            rightValues = GTC_BASE_TOKENS;
        }

        const uint64_t combinations =
            uint64_t(leftValues) *
            uint64_t(rightValues);

        uint32_t pairBits = 0;

        uint64_t v =
            combinations > 0
                ? combinations - 1
                : 0;

        do {
            ++pairBits;
            v >>= 1;
        } while (v != 0);

        pairLowerBoundBits += pairBits;
        categoryPairBits[category] += pairBits;
    }

    const uint64_t actualBits =
        actualLeftBits + actualRightBits;

    const uint64_t actualBytes =
        (actualBits + 7) / 8;

    const uint64_t commonBytes =
        (commonWidthBits + 7) / 8;

    const uint64_t pairBytes =
        (pairLowerBoundBits + 7) / 8;

    std::printf("\n");
    std::printf(
        "Operand bit-width analysis\n");
    std::printf(
        "--------------------------\n");

    std::printf(
        "rules                  : %u\n",
        ruleCount);

    std::printf(
        "actual left bits       : %llu\n",
        static_cast<unsigned long long>(
            actualLeftBits));

    std::printf(
        "actual right bits      : %llu\n",
        static_cast<unsigned long long>(
            actualRightBits));

    std::printf(
        "actual operand bits    : %llu\n",
        static_cast<unsigned long long>(
            actualBits));

    std::printf(
        "actual operand bytes   : %llu\n",
        static_cast<unsigned long long>(
            actualBytes));

    std::printf("\n");

    std::printf(
        "common-width bits      : %llu\n",
        static_cast<unsigned long long>(
            commonWidthBits));

    std::printf(
        "common-width bytes     : %llu\n",
        static_cast<unsigned long long>(
            commonBytes));

    std::printf("\n");

    std::printf(
        "pair lower-bound bits  : %llu\n",
        static_cast<unsigned long long>(
            pairLowerBoundBits));

    std::printf(
        "pair lower-bound bytes : %llu\n",
        static_cast<unsigned long long>(
            pairBytes));

    std::printf("\n");

    std::printf(
        "uint16 grammar bytes   : %llu\n",
        static_cast<unsigned long long>(
            uint64_t(ruleCount) * 4));

    std::printf("\n");

    std::printf(
        "Maximum operand width\n");
    std::printf(
        "---------------------\n");
    std::printf(
        "left                  : %u bits\n",
        maxLeftBits);
    std::printf(
        "right                 : %u bits\n",
        maxRightBits);

    std::printf("\n");

    std::printf(
        "Rule categories\n");
    std::printf(
        "---------------\n");

    std::printf(
        "%-4s %8s %12s %12s %12s %12s\n",
        "type",
        "rules",
        "actual",
        "common",
        "pair LB",
        "avg actual");

    const char* names[4] = {
        "BB",
        "BG",
        "GB",
        "GG"
    };

    for (uint32_t c = 0; c < 4; ++c) {
        if (categoryCount[c] == 0)
            continue;

        const double avgActual =
            static_cast<double>(
                categoryActualBits[c]) /
            static_cast<double>(
                categoryCount[c]);

        std::printf(
            "%-4s %8llu %12llu %12llu %12llu %11.3f\n",
            names[c],
            static_cast<unsigned long long>(
                categoryCount[c]),
            static_cast<unsigned long long>(
                categoryActualBits[c]),
            static_cast<unsigned long long>(
                categoryCommonBits[c]),
            static_cast<unsigned long long>(
                categoryPairBits[c]),
            avgActual);
    }

    std::printf("\n");

    std::printf(
        "Potential savings vs uint16\n");
    std::printf(
        "---------------------------\n");

    if (uint64_t(ruleCount) * 4 >= actualBytes) {
        std::printf(
            "independent operands : %llu bytes\n",
            static_cast<unsigned long long>(
                uint64_t(ruleCount) * 4 -
                actualBytes));
    }

    if (uint64_t(ruleCount) * 4 >= commonBytes) {
        std::printf(
            "common dynamic width : %llu bytes\n",
            static_cast<unsigned long long>(
                uint64_t(ruleCount) * 4 -
                commonBytes));
    }

    if (uint64_t(ruleCount) * 4 >= pairBytes) {
        std::printf(
            "pair lower bound     : %llu bytes\n",
            static_cast<unsigned long long>(
                uint64_t(ruleCount) * 4 -
                pairBytes));
    }
}
static void analyzeGrammarPairs(
    const GtcDictionary& dictionary)
{
    const uint32_t ruleCount =
        static_cast<uint32_t>(
            dictionary.size());

    uint64_t leftTerminal = 0;
    uint64_t rightTerminal = 0;

    uint64_t bothTerminal = 0;
    uint64_t bothGrammar = 0;

    uint64_t leftLessRight = 0;
    uint64_t leftGreaterRight = 0;
    uint64_t leftEqualRight = 0;

    uint64_t leftIsPreviousToken = 0;
    uint64_t rightIsPreviousToken = 0;

    uint64_t leftDistance[17] = {};
    uint64_t rightDistance[17] = {};

    uint64_t pairSameType = 0;

    uint64_t adjacentGrammar =
        0;

    uint64_t leftByteRightGrammar =
        0;

    uint64_t leftGrammarRightByte =
        0;

    uint64_t minDifference =
        UINT64_MAX;

    uint64_t maxDifference = 0;

    uint64_t differenceHistogram[8192] = {};

    /*
     * Exact pair frequency.
     *
     * A pair is encoded into uint64_t:
     *
     * high 32 bits = left
     * low  32 bits = right
     */
    std::unordered_map<
        uint64_t,
        uint32_t> pairFrequency;

    pairFrequency.reserve(
        ruleCount * 2);

    for (uint32_t i = 0;
         i < ruleCount;
         ++i) {

        const uint32_t currentToken =
            GTC_BASE_TOKENS + i;

        const GtcRule& rule =
            dictionary.rule(
                currentToken);

        const uint32_t left =
            rule.left;

        const uint32_t right =
            rule.right;

        const bool leftTerm =
            left < GTC_BASE_TOKENS;

        const bool rightTerm =
            right < GTC_BASE_TOKENS;

        if (leftTerm)
            ++leftTerminal;

        if (rightTerm)
            ++rightTerminal;

        if (leftTerm && rightTerm)
            ++bothTerminal;

        if (!leftTerm && !rightTerm)
            ++bothGrammar;

        if (left < right)
            ++leftLessRight;
        else if (left > right)
            ++leftGreaterRight;
        else
            ++leftEqualRight;

        if (left == currentToken - 1)
            ++leftIsPreviousToken;

        if (right == currentToken - 1)
            ++rightIsPreviousToken;

        if (leftTerm && !rightTerm)
            ++leftByteRightGrammar;

        if (!leftTerm && rightTerm)
            ++leftGrammarRightByte;

        if ((left < GTC_BASE_TOKENS) ==
            (right < GTC_BASE_TOKENS))
            ++pairSameType;

        /*
         * Grammar-to-grammar adjacency.
         *
         * Examples:
         *
         * T300 = T299 + T298
         */
        if (left >= GTC_BASE_TOKENS &&
            right >= GTC_BASE_TOKENS) {

            const uint32_t distance =
                left > right
                    ? left - right
                    : right - left;

            if (distance < 8192)
                ++differenceHistogram[
                    distance];

            if (distance < minDifference)
                minDifference = distance;

            if (distance > maxDifference)
                maxDifference = distance;

            if (distance == 1)
                ++adjacentGrammar;
        }

        /*
         * Distance from current rule.
         */
        const uint32_t leftDistanceValue =
            currentToken - left;

        const uint32_t rightDistanceValue =
            currentToken - right;

        auto bitsRequired =
            [](uint32_t value) -> unsigned
        {
            unsigned bits = 0;

            do {
                ++bits;
                value >>= 1;
            } while (value != 0);

            return bits;
        };

        const unsigned lb =
            bitsRequired(
                leftDistanceValue);

        const unsigned rb =
            bitsRequired(
                rightDistanceValue);

        if (lb <= 16)
            ++leftDistance[lb];

        if (rb <= 16)
            ++rightDistance[rb];

        const uint64_t pair =
            (static_cast<uint64_t>(left) << 32) |
            static_cast<uint64_t>(right);

        ++pairFrequency[pair];
    }

    const uint64_t references =
        static_cast<uint64_t>(
            ruleCount) * 2ULL;

    auto percent =
        [](uint64_t value,
           uint64_t total) -> double
    {
        if (total == 0)
            return 0.0;

        return
            100.0 *
            static_cast<double>(value) /
            static_cast<double>(total);
    };

    std::printf(
        "\n"
        "Grammar pair analysis\n"
        "---------------------\n"
        "rules                  : %u\n"
        "pairs                  : %llu\n"
        "\n",
        ruleCount,
        static_cast<unsigned long long>(
            references));

    std::printf(
        "Operand types\n"
        "-------------\n"
        "left  terminal (<256)  : %llu (%.2f%%)\n"
        "right terminal (<256)  : %llu (%.2f%%)\n"
        "both terminal           : %llu (%.2f%% of pairs)\n"
        "both grammar            : %llu (%.2f%% of pairs)\n"
        "byte -> grammar         : %llu (%.2f%% of pairs)\n"
        "grammar -> byte         : %llu (%.2f%% of pairs)\n"
        "same operand type       : %llu (%.2f%% of pairs)\n"
        "\n",
        static_cast<unsigned long long>(
            leftTerminal),
        percent(leftTerminal, ruleCount),

        static_cast<unsigned long long>(
            rightTerminal),
        percent(rightTerminal, ruleCount),

        static_cast<unsigned long long>(
            bothTerminal),
        percent(bothTerminal, ruleCount),

        static_cast<unsigned long long>(
            bothGrammar),
        percent(bothGrammar, ruleCount),

        static_cast<unsigned long long>(
            leftByteRightGrammar),
        percent(
            leftByteRightGrammar,
            ruleCount),

        static_cast<unsigned long long>(
            leftGrammarRightByte),
        percent(
            leftGrammarRightByte,
            ruleCount),

        static_cast<unsigned long long>(
            pairSameType),
        percent(
            pairSameType,
            ruleCount));

    std::printf(
        "Ordering\n"
        "--------\n"
        "left < right           : %llu (%.2f%%)\n"
        "left > right           : %llu (%.2f%%)\n"
        "left == right          : %llu (%.2f%%)\n"
        "\n",
        static_cast<unsigned long long>(
            leftLessRight),
        percent(leftLessRight, ruleCount),

        static_cast<unsigned long long>(
            leftGreaterRight),
        percent(
            leftGreaterRight,
            ruleCount),

        static_cast<unsigned long long>(
            leftEqualRight),
        percent(leftEqualRight, ruleCount));

    std::printf(
        "Previous-token references\n"
        "-------------------------\n"
        "left  == current-1     : %llu (%.2f%%)\n"
        "right == current-1     : %llu (%.2f%%)\n"
        "\n",
        static_cast<unsigned long long>(
            leftIsPreviousToken),
        percent(
            leftIsPreviousToken,
            ruleCount),

        static_cast<unsigned long long>(
            rightIsPreviousToken),
        percent(
            rightIsPreviousToken,
            ruleCount));

    std::printf(
        "Grammar/grammar distances\n"
        "--------------------------\n"
        "adjacent grammar IDs    : %llu\n"
        "minimum distance        : %llu\n"
        "maximum distance        : %llu\n"
        "\n",
        static_cast<unsigned long long>(
            adjacentGrammar),

        minDifference ==
                UINT64_MAX
            ? 0ULL
            : static_cast<unsigned long long>(
                  minDifference),

        static_cast<unsigned long long>(
            maxDifference));

    std::printf(
        "Distance histogram for grammar/grammar pairs\n"
        "---------------------------------------------\n"
        "distance   count\n");

    for (uint32_t distance = 0;
         distance < 8192;
         ++distance) {

        if (differenceHistogram[distance] == 0)
            continue;

        if (distance <= 32 ||
            differenceHistogram[distance] >= 20) {

            std::printf(
                "%8u   %8llu\n",
                distance,
                static_cast<unsigned long long>(
                    differenceHistogram[
                        distance]));
        }
    }

    std::printf(
        "\n"
        "Distance from current token\n"
        "----------------------------\n"
        "bits   left        right\n");

    for (unsigned bits = 1;
         bits <= 16;
         ++bits) {

        if (leftDistance[bits] == 0 &&
            rightDistance[bits] == 0)
            continue;

        std::printf(
            "%2u   %8llu   %8llu\n",
            bits,
            static_cast<unsigned long long>(
                leftDistance[bits]),
            static_cast<unsigned long long>(
                rightDistance[bits]));
    }

    /*
     * Sort pair frequencies.
     */
    struct PairFrequency {
        uint32_t left;
        uint32_t right;
        uint32_t count;
    };

    std::vector<PairFrequency> pairs;

    pairs.reserve(
        pairFrequency.size());

    for (const auto& entry :
         pairFrequency) {

        const uint32_t left =
            static_cast<uint32_t>(
                entry.first >> 32);

        const uint32_t right =
            static_cast<uint32_t>(
                entry.first & 0xffffffffULL);

        pairs.push_back({
            left,
            right,
            entry.second
        });
    }

    std::sort(
        pairs.begin(),
        pairs.end(),
        [](const PairFrequency& a,
           const PairFrequency& b)
        {
            return a.count > b.count;
        });

    std::printf(
        "\n"
        "Repeated grammar pairs\n"
        "-----------------------\n");

    uint64_t repeatedRules = 0;
    uint64_t repeatedExtra = 0;

    for (const PairFrequency& pair :
         pairs) {

        if (pair.count > 1) {
            repeatedRules += pair.count;
            repeatedExtra +=
                pair.count - 1;
        }
    }

    std::printf(
        "rules belonging to repeated pairs : %llu\n"
        "extra occurrences                  : %llu\n"
        "unique pairs                       : %llu\n"
        "\n",
        static_cast<unsigned long long>(
            repeatedRules),

        static_cast<unsigned long long>(
            repeatedExtra),

        static_cast<unsigned long long>(
            pairs.size()));

    const size_t top =
        pairs.size() < 30
            ? pairs.size()
            : 30;

    for (size_t i = 0;
         i < top;
         ++i) {

        std::printf(
            "%2u: (%5u, %5u)  %u\n",
            static_cast<unsigned>(i + 1),
            pairs[i].left,
            pairs[i].right,
            pairs[i].count);
    }

    std::printf("\n");
}
static void analyzeGrammar(
    const GtcDictionary& dictionary)
{
    const uint32_t ruleCount =
        static_cast<uint32_t>(
            dictionary.size());

    uint64_t leftBits[17] = {};
    uint64_t rightBits[17] = {};

    uint64_t leftVarintBytes = 0;
    uint64_t rightVarintBytes = 0;

    uint64_t leftDeltaBits[17] = {};
    uint64_t rightDeltaBits[17] = {};

    uint64_t leftDeltaVarintBytes = 0;
    uint64_t rightDeltaVarintBytes = 0;

    uint64_t leftFrequency[65536] = {};
    uint64_t rightFrequency[65536] = {};

    uint64_t leftSmall256 = 0;
    uint64_t rightSmall256 = 0;

    uint64_t leftSmall512 = 0;
    uint64_t rightSmall512 = 0;

    uint64_t leftSmall1024 = 0;
    uint64_t rightSmall1024 = 0;

    uint64_t leftSmall2048 = 0;
    uint64_t rightSmall2048 = 0;

    uint64_t leftSmall4096 = 0;
    uint64_t rightSmall4096 = 0;

    uint64_t rightSmall4096Plus = 0;

    uint32_t maxLeft = 0;
    uint32_t maxRight = 0;

    uint32_t maxLeftDelta = 0;
    uint32_t maxRightDelta = 0;

    auto bitsRequired =
        [](uint32_t value) -> unsigned
    {
        unsigned bits = 0;

        do {
            ++bits;
            value >>= 1;
        } while (value != 0);

        return bits;
    };

    auto varintBytes =
        [](uint32_t value) -> unsigned
    {
        unsigned bytes = 1;

        while (value >= 128) {
            value >>= 7;
            ++bytes;
        }

        return bytes;
    };

    for (uint32_t i = 0;
         i < ruleCount;
         ++i) {

        const uint32_t currentToken =
            GTC_BASE_TOKENS + i;

        const GtcRule& rule =
            dictionary.rule(currentToken);

        const uint32_t left =
            rule.left;

        const uint32_t right =
            rule.right;

        const unsigned lb =
            bitsRequired(left);

        const unsigned rb =
            bitsRequired(right);

        ++leftBits[lb];
        ++rightBits[rb];

        leftVarintBytes +=
            varintBytes(left);

        rightVarintBytes +=
            varintBytes(right);

        ++leftFrequency[left];
        ++rightFrequency[right];

        if (left < 256)
            ++leftSmall256;

        if (right < 256)
            ++rightSmall256;

        if (left < 512)
            ++leftSmall512;

        if (right < 512)
            ++rightSmall512;

        if (left < 1024)
            ++leftSmall1024;

        if (right < 1024)
            ++rightSmall1024;

        if (left < 2048)
            ++leftSmall2048;

        if (right < 2048)
            ++rightSmall2048;

        if (left < 4096)
            ++leftSmall4096;

        if (right < 4096)
            ++rightSmall4096;

        if (right >= 4096)
            ++rightSmall4096Plus;

        if (left > maxLeft)
            maxLeft = left;

        if (right > maxRight)
            maxRight = right;

        /*
         * Delta from the current rule number.
         *
         * currentToken > reference
         */
        const uint32_t leftDelta =
            currentToken - left;

        const uint32_t rightDelta =
            currentToken - right;

        const unsigned ldb =
            bitsRequired(leftDelta);

        const unsigned rdb =
            bitsRequired(rightDelta);

        ++leftDeltaBits[ldb];
        ++rightDeltaBits[rdb];

        leftDeltaVarintBytes +=
            varintBytes(leftDelta);

        rightDeltaVarintBytes +=
            varintBytes(rightDelta);

        if (leftDelta > maxLeftDelta)
            maxLeftDelta = leftDelta;

        if (rightDelta > maxRightDelta)
            maxRightDelta = rightDelta;
    }

    const uint64_t references =
        static_cast<uint64_t>(ruleCount) * 2ULL;

    auto percent =
        [references](uint64_t value) -> double
    {
        if (references == 0)
            return 0.0;

        return
            100.0 *
            static_cast<double>(value) /
            static_cast<double>(references);
    };

    std::printf(
        "\n"
        "Grammar analysis\n"
        "----------------\n"
        "rules                 : %u\n"
        "references            : %llu\n"
        "current grammar size  : %llu bytes\n"
        "\n",
        ruleCount,
        static_cast<unsigned long long>(
            references),
        static_cast<unsigned long long>(
            references * 2ULL));

    std::printf(
        "Reference ranges\n"
        "----------------\n"
        "left  < 256          : %llu (%.2f%%)\n"
        "right < 256          : %llu (%.2f%%)\n"
        "left  < 512          : %llu (%.2f%%)\n"
        "right < 512          : %llu (%.2f%%)\n"
        "left  < 1024         : %llu (%.2f%%)\n"
        "right < 1024         : %llu (%.2f%%)\n"
        "left  < 2048         : %llu (%.2f%%)\n"
        "right < 2048         : %llu (%.2f%%)\n"
        "left  < 4096         : %llu (%.2f%%)\n"
        "right < 4096         : %llu (%.2f%%)\n"
        "right >= 4096        : %llu (%.2f%%)\n"
        "\n",
        static_cast<unsigned long long>(
            leftSmall256),
        percent(leftSmall256),
        static_cast<unsigned long long>(
            rightSmall256),
        percent(rightSmall256),

        static_cast<unsigned long long>(
            leftSmall512),
        percent(leftSmall512),
        static_cast<unsigned long long>(
            rightSmall512),
        percent(rightSmall512),

        static_cast<unsigned long long>(
            leftSmall1024),
        percent(leftSmall1024),
        static_cast<unsigned long long>(
            rightSmall1024),
        percent(rightSmall1024),

        static_cast<unsigned long long>(
            leftSmall2048),
        percent(leftSmall2048),
        static_cast<unsigned long long>(
            rightSmall2048),
        percent(rightSmall2048),

        static_cast<unsigned long long>(
            leftSmall4096),
        percent(leftSmall4096),
        static_cast<unsigned long long>(
            rightSmall4096),
        percent(rightSmall4096),

        static_cast<unsigned long long>(
            rightSmall4096Plus),
        percent(rightSmall4096Plus));

    std::printf(
        "Bit width distribution\n"
        "----------------------\n"
        "bits   left        right\n");

    for (unsigned bits = 1;
         bits <= 16;
         ++bits) {

        if (leftBits[bits] == 0 &&
            rightBits[bits] == 0)
            continue;

        std::printf(
            "%2u   %8llu   %8llu\n",
            bits,
            static_cast<unsigned long long>(
                leftBits[bits]),
            static_cast<unsigned long long>(
                rightBits[bits]));
    }

    std::printf(
        "\n"
        "Variable-length estimate\n"
        "------------------------\n"
        "uint16 grammar         : %llu bytes\n"
        "7-bit varint grammar   : %llu bytes\n"
        "7-bit varint saving    : %lld bytes\n"
        "\n",
        static_cast<unsigned long long>(
            references * 2ULL),

        static_cast<unsigned long long>(
            leftVarintBytes +
            rightVarintBytes),

        static_cast<long long>(
            references * 2ULL -
            (leftVarintBytes +
             rightVarintBytes)));

    std::printf(
        "Delta coding\n"
        "------------\n"
        "max left delta         : %u\n"
        "max right delta        : %u\n"
        "delta varint grammar   : %llu bytes\n"
        "delta varint saving    : %lld bytes\n"
        "\n",
        maxLeftDelta,
        maxRightDelta,

        static_cast<unsigned long long>(
            leftDeltaVarintBytes +
            rightDeltaVarintBytes),

        static_cast<long long>(
            references * 2ULL -
            (leftDeltaVarintBytes +
             rightDeltaVarintBytes)));

    std::printf(
        "Delta bit width\n"
        "---------------\n"
        "bits   left        right\n");

    for (unsigned bits = 1;
         bits <= 16;
         ++bits) {

        if (leftDeltaBits[bits] == 0 &&
            rightDeltaBits[bits] == 0)
            continue;

        std::printf(
            "%2u   %8llu   %8llu\n",
            bits,
            static_cast<unsigned long long>(
                leftDeltaBits[bits]),
            static_cast<unsigned long long>(
                rightDeltaBits[bits]));
    }

    struct Frequency {
        uint32_t token;
        uint64_t count;
    };

    std::vector<Frequency> frequencies;

    frequencies.reserve(65536);

    for (uint32_t token = 0;
         token < 65536;
         ++token) {

        if (leftFrequency[token] != 0) {
            frequencies.push_back({
                token,
                leftFrequency[token]
            });
        }
    }

    std::sort(
        frequencies.begin(),
        frequencies.end(),
        [](const Frequency& a,
           const Frequency& b)
        {
            return a.count > b.count;
        });

    std::printf(
        "\n"
        "TOP LEFT references\n"
        "-------------------\n");

    const size_t leftTop =
        frequencies.size() < 30
            ? frequencies.size()
            : 30;

    for (size_t i = 0;
         i < leftTop;
         ++i) {

        std::printf(
            "%2u: token %5u  %8llu\n",
            static_cast<unsigned>(i + 1),
            frequencies[i].token,
            static_cast<unsigned long long>(
                frequencies[i].count));
    }

    frequencies.clear();

    for (uint32_t token = 0;
         token < 65536;
         ++token) {

        if (rightFrequency[token] != 0) {
            frequencies.push_back({
                token,
                rightFrequency[token]
            });
        }
    }

    std::sort(
        frequencies.begin(),
        frequencies.end(),
        [](const Frequency& a,
           const Frequency& b)
        {
            return a.count > b.count;
        });

    std::printf(
        "\n"
        "TOP RIGHT references\n"
        "--------------------\n");

    const size_t rightTop =
        frequencies.size() < 30
            ? frequencies.size()
            : 30;

    for (size_t i = 0;
         i < rightTop;
         ++i) {

        std::printf(
            "%2u: token %5u  %8llu\n",
            static_cast<unsigned>(i + 1),
            frequencies[i].token,
            static_cast<unsigned long long>(
                frequencies[i].count));
    }

    std::printf("\n");
}
void markReachableRules(
    const std::vector<uint32_t>& sequence,
    const GtcDictionary& dictionary,
    std::vector<uint8_t>& reachable)
{
    reachable.assign(
        dictionary.size(),
        0);

    std::vector<uint32_t> stack;

    for (uint32_t token : sequence) {
        if (token >= GTC_BASE_TOKENS)
            stack.push_back(token);
    }

    while (!stack.empty()) {
        uint32_t token =
            stack.back();

        stack.pop_back();

        if (token < GTC_BASE_TOKENS)
            continue;

        uint32_t index =
            token - GTC_BASE_TOKENS;

        if (index >= dictionary.size())
            continue;

        if (reachable[index])
            continue;

        reachable[index] = 1;

        const GtcRule& rule =
            dictionary.rule(token);

        if (rule.left >= GTC_BASE_TOKENS)
            stack.push_back(rule.left);

        if (rule.right >= GTC_BASE_TOKENS)
            stack.push_back(rule.right);
    }
}
uint64_t ruleExpansionSize(
    uint32_t token,
    const GtcDictionary& dictionary,
    std::vector<uint64_t>& cache)
{
    if (token < GTC_BASE_TOKENS)
        return 1;

    uint32_t index =
        token - GTC_BASE_TOKENS;

    if (cache[index] != 0)
        return cache[index];

    const GtcRule& rule =
        dictionary.rule(token);

    uint64_t size =
        ruleExpansionSize(
            rule.left,
            dictionary,
            cache) +
        ruleExpansionSize(
            rule.right,
            dictionary,
            cache);

    cache[index] = size;

    return size;
}
static uint64_t countExpandedRuleOccurrences(
    uint32_t target,
    const GtcDictionary& dictionary,
    const std::vector<uint32_t>& sequence)
{
    std::vector<uint32_t> stack;

    uint64_t count = 0;

    for (uint32_t token : sequence) {

        if (token < GTC_BASE_TOKENS)
            continue;

        stack.push_back(token);
    }

    while (!stack.empty()) {

        uint32_t token =
            stack.back();

        stack.pop_back();

        if (token == target)
            ++count;

        if (token < GTC_BASE_TOKENS)
            continue;

        const GtcRule& rule =
            dictionary.rule(token);

        if (rule.left >= GTC_BASE_TOKENS)
            stack.push_back(rule.left);

        if (rule.right >= GTC_BASE_TOKENS)
            stack.push_back(rule.right);
    }

    return count;
}
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
static uint64_t countExpandedBytePair(
    uint8_t first,
    uint8_t second,
    const GtcDictionary& dictionary,
    const std::vector<uint32_t>& sequence)
{
    std::vector<uint32_t> stack;

    uint64_t count = 0;

    /*
     * Stack contains tokens to be expanded.
     *
     * We need to detect adjacent terminal bytes, so keep
     * the previous terminal byte while walking each expanded
     * sequence.
     *
     * The simplest reliable way is to recursively expand each
     * final token into a temporary terminal-byte stream.
     */
    std::vector<uint8_t> expanded;

    for (uint32_t token : sequence) {
        stack.push_back(token);

        while (!stack.empty()) {
            uint32_t current = stack.back();
            stack.pop_back();

            if (current < GTC_BASE_TOKENS) {
                expanded.push_back(
                    static_cast<uint8_t>(current));
            } else {
                const GtcRule& rule =
                    dictionary.rule(current);

                /*
                 * Push right first so left is processed first.
                 */
                stack.push_back(rule.right);
                stack.push_back(rule.left);
            }
        }
    }

    for (size_t i = 0; i + 1 < expanded.size(); ++i) {
        if (expanded[i] == first &&
            expanded[i + 1] == second) {
            ++count;
        }
    }

    return count;
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

uint32_t iteration = 0;
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

        const size_t oldSize =
            sequence.size();

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

std::vector<uint64_t> expansionCache(
    dictionary.size(),
    0);

uint64_t expansion =
    ruleExpansionSize(
        newToken,
        dictionary,
        expansionCache);

std::printf(
    "%6u  %6u %6u  count=%6u  expanded=%6llu  %zu -> %zu\n",
    iteration++,
    best.left,
    best.right,
    best.count,
    static_cast<unsigned long long>(
        expansion),
    oldSize,
    sequence.size());
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
std::vector<uint32_t> streamUses(
    dictionary.size(),
    0);

for (uint32_t token : sequence) {
    if (token < GTC_BASE_TOKENS)
        continue;

    uint32_t index =
        token - GTC_BASE_TOKENS;

    if (index < dictionary.size())
        ++streamUses[index];
}
{
    uint32_t token = 265;

    uint64_t occurrences =
        countExpandedRuleOccurrences(
            token,
            dictionary,
            sequence);

    std::printf(
        "\nRule 265 coverage:\n"
        "direct final uses : %u\n"
        "all references    : %llu\n",
        streamUses[
            token - GTC_BASE_TOKENS],
        static_cast<unsigned long long>(
            occurrences));
}
{
    uint64_t occurrences =
        countExpandedBytePair(
            44,
            32,
            dictionary,
            sequence);

    std::printf(
        "\nExpanded byte pair coverage:\n"
        "pair              : 44 32\n"
        "occurrences        : %llu\n",
        static_cast<unsigned long long>(
            occurrences));
}
    std::vector<uint32_t> childUses(
        dictionary.size(),
        0);

    for (uint32_t i = 0;
         i < dictionary.size();
         ++i) {

        uint32_t token =
            GTC_BASE_TOKENS + i;

        const GtcRule& rule =
            dictionary.rule(token);

        if (rule.left >= GTC_BASE_TOKENS) {
            uint32_t index =
                rule.left - GTC_BASE_TOKENS;

            if (index < dictionary.size())
                ++childUses[index];
        }

        if (rule.right >= GTC_BASE_TOKENS) {
            uint32_t index =
                rule.right - GTC_BASE_TOKENS;

            if (index < dictionary.size())
                ++childUses[index];
        }
    }
    {
        uint32_t token = 265;
        uint32_t index =
            token - GTC_BASE_TOKENS;

        if (index < dictionary.size()) {

            std::printf(
                "\nRule 265 usage:\n"
                "rule             : %u\n"
                "direct uses      : %u\n"
                "child uses       : %u\n"
                "total references: %u\n",
                token,
                streamUses[index],
                childUses[index],
                streamUses[index] +
                childUses[index]);
        }
    }
uint64_t totalRuleSavingBits = 0;

uint64_t saving0_7 = 0;
uint64_t saving8_15 = 0;
uint64_t saving16_31 = 0;
uint64_t saving32_63 = 0;
uint64_t saving64_127 = 0;
uint64_t saving128_255 = 0;
uint64_t saving256plus = 0;

uint64_t totalRuleUses = 0;

uint64_t maxRuleSaving = 0;

uint32_t maxRuleSavingToken = 0;

for (uint32_t i = 0;
     i < dictionary.size();
     ++i) {

    uint64_t uses =
        streamUses[i];

    if (uses == 0)
        continue;

    uint32_t token =
        GTC_BASE_TOKENS + i;

const GtcRule& rule =
    dictionary.rule(token);

uint64_t childBits =
    static_cast<uint64_t>(
        huffman.codeLength(rule.left)) +
    static_cast<uint64_t>(
        huffman.codeLength(rule.right));

    uint64_t tokenBits =
        huffman.codeLength(token);

    if (childBits <= tokenBits)
        continue;

    uint64_t savingPerUse =
        childBits - tokenBits;

    uint64_t totalSaving =
        savingPerUse * uses;

    totalRuleUses += uses;

    totalRuleSavingBits +=
        totalSaving;

    if (totalSaving > maxRuleSaving) {
        maxRuleSaving =
            totalSaving;

        maxRuleSavingToken =
            token;
    }

    if (totalSaving <= 7)
        ++saving0_7;
    else if (totalSaving <= 15)
        ++saving8_15;
    else if (totalSaving <= 31)
        ++saving16_31;
    else if (totalSaving <= 63)
        ++saving32_63;
    else if (totalSaving <= 127)
        ++saving64_127;
    else if (totalSaving <= 255)
        ++saving128_255;
    else
        ++saving256plus;
}

std::printf(
    "\nGrammar Huffman value:\n"
    "rule uses        : %llu\n"
    "saving           : %llu bits\n"
    "saving           : %.2f bytes\n"
    "max rule saving  : %llu bits\n"
    "max token        : %u\n"
    "\n"
    "Rules by saving:\n"
    "0-7 bits         : %llu\n"
    "8-15             : %llu\n"
    "16-31            : %llu\n"
    "32-63            : %llu\n"
    "64-127           : %llu\n"
    "128-255          : %llu\n"
    "256+             : %llu\n",
    static_cast<unsigned long long>(
        totalRuleUses),
    static_cast<unsigned long long>(
        totalRuleSavingBits),
    static_cast<double>(
        totalRuleSavingBits) / 8.0,
    static_cast<unsigned long long>(
        maxRuleSaving),
    maxRuleSavingToken,
    static_cast<unsigned long long>(
        saving0_7),
    static_cast<unsigned long long>(
        saving8_15),
    static_cast<unsigned long long>(
        saving16_31),
    static_cast<unsigned long long>(
        saving32_63),
    static_cast<unsigned long long>(
        saving64_127),
    static_cast<unsigned long long>(
        saving128_255),
    static_cast<unsigned long long>(
        saving256plus));
struct RuleValueInfo {
    uint32_t token;
    uint32_t uses;
    uint64_t expanded;

    uint32_t left;
    uint32_t right;

    uint16_t leftBits;
    uint16_t rightBits;
    uint16_t tokenBits;

    uint64_t savingPerUse;
    uint64_t totalSaving;
};
    std::vector<RuleValueInfo> topRules;

    std::vector<uint64_t> expansionCache(
        dictionary.size(),
        0);

    for (uint32_t i = 0;
         i < dictionary.size();
         ++i) {

        uint32_t uses =
            streamUses[i];

        if (uses == 0)
            continue;

        uint32_t token =
            GTC_BASE_TOKENS + i;

        const GtcRule& rule =
            dictionary.rule(token);

        uint16_t leftBits =
            huffman.codeLength(rule.left);

        uint16_t rightBits =
            huffman.codeLength(rule.right);

        uint16_t tokenBits =
            huffman.codeLength(token);

        uint64_t childBits =
            static_cast<uint64_t>(leftBits) +
            static_cast<uint64_t>(rightBits);

        if (childBits <= tokenBits)
            continue;

        uint64_t savingPerUse =
            childBits - tokenBits;

        uint64_t totalSaving =
            savingPerUse *
            static_cast<uint64_t>(uses);

        uint64_t expanded =
            ruleExpansionSize(
                token,
                dictionary,
                expansionCache);

RuleValueInfo info;

info.token = token;
info.uses = uses;
info.expanded = expanded;

info.left = rule.left;
info.right = rule.right;

info.leftBits = leftBits;
info.rightBits = rightBits;
info.tokenBits = tokenBits;

info.savingPerUse = savingPerUse;
info.totalSaving = totalSaving;

        topRules.push_back(info);
    }

    for (size_t i = 0;
         i < topRules.size();
         ++i) {

        size_t best = i;

        for (size_t j = i + 1;
             j < topRules.size();
             ++j) {

            if (topRules[j].totalSaving >
                topRules[best].totalSaving) {

                best = j;
            }
        }

        if (best != i) {
            RuleValueInfo tmp =
                topRules[i];

            topRules[i] =
                topRules[best];

            topRules[best] =
                tmp;
        }
    }

    size_t topCount =
        topRules.size() < 30
            ? topRules.size()
            : 30;

std::printf(
    "\nTop grammar rules by Huffman value:\n"
    "\n"
    "token  uses  expanded  "
    "left right  "
    "Lbits Rbits Tbits  "
    "save/use  total\n");

    for (size_t i = 0;
         i < topCount;
         ++i) {

        const RuleValueInfo& info =
            topRules[i];

std::printf(
    "%5u %5u %9llu  "
    "%5u %5u  "
    "%5u %5u %5u  "
    "%8llu %8llu\n",
info.token,
info.uses,
static_cast<unsigned long long>(
    info.expanded),
info.left,
info.right,
info.leftBits,
info.rightBits,
info.tokenBits,
static_cast<unsigned long long>(
    info.savingPerUse),
static_cast<unsigned long long>(
    info.totalSaving));
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

void countRuleUses(
    const std::vector<uint32_t>& sequence,
    const GtcDictionary& dictionary,
    std::vector<uint32_t>& uses)
{
    uses.assign(
        dictionary.size(),
        0);

    for (uint32_t token : sequence) {
        if (token >= GTC_BASE_TOKENS) {
            uint32_t index =
                token - GTC_BASE_TOKENS;

            if (index < dictionary.size())
                ++uses[index];
        }
    }

    for (uint32_t i = 0;
         i < dictionary.size();
         ++i) {

        uint32_t token =
            GTC_BASE_TOKENS + i;

        const GtcRule& rule =
            dictionary.rule(token);

        if (rule.left >= GTC_BASE_TOKENS) {
            uint32_t index =
                rule.left - GTC_BASE_TOKENS;

            if (index < dictionary.size())
                ++uses[index];
        }

        if (rule.right >= GTC_BASE_TOKENS) {
            uint32_t index =
                rule.right - GTC_BASE_TOKENS;

            if (index < dictionary.size())
                ++uses[index];
        }
    }
}

uint32_t singleUseChainDepth(
    uint32_t token,
    const GtcDictionary& dictionary,
    const std::vector<uint32_t>& uses)
{
    uint32_t depth = 0;

    while (token >= GTC_BASE_TOKENS) {
        uint32_t index =
            token - GTC_BASE_TOKENS;

        if (index >= dictionary.size())
            break;

        if (uses[index] != 1)
            break;

        ++depth;

        const GtcRule& rule =
            dictionary.rule(token);

        /*
         * A rule has two children.
         *
         * For a chain we follow the child that is
         * itself a single-use grammar rule.
         *
         * If both children qualify, this is a branch,
         * not a simple chain, so stop here.
         */

        bool leftRule =
            rule.left >= GTC_BASE_TOKENS &&
            uses[rule.left - GTC_BASE_TOKENS] == 1;

        bool rightRule =
            rule.right >= GTC_BASE_TOKENS &&
            uses[rule.right - GTC_BASE_TOKENS] == 1;

        if (leftRule && rightRule)
            break;

        if (leftRule) {
            token = rule.left;
            continue;
        }

        if (rightRule) {
            token = rule.right;
            continue;
        }

        break;
    }

    return depth;
}
uint32_t singleUseSubtreeSize(
    uint32_t token,
    const GtcDictionary& dictionary,
    const std::vector<uint32_t>& uses)
{
    if (token < GTC_BASE_TOKENS)
        return 0;

    uint32_t index =
        token - GTC_BASE_TOKENS;

    if (index >= dictionary.size())
        return 0;

    if (uses[index] != 1)
        return 0;

    const GtcRule& rule =
        dictionary.rule(token);

    uint32_t size = 1;

    if (rule.left >= GTC_BASE_TOKENS) {
        size += singleUseSubtreeSize(
            rule.left,
            dictionary,
            uses);
    }

    if (rule.right >= GTC_BASE_TOKENS) {
        size += singleUseSubtreeSize(
            rule.right,
            dictionary,
            uses);
    }

    return size;
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

        std::cerr
            << "error: cannot read "
            << inputName
            << "\n";

        return false;
    }

    GtcDictionary dictionary;

    std::vector<uint32_t> sequence;

    buildGrammar(
        input,
        dictionary,
        sequence);

analyzeGrammar(dictionary);
analyzeGrammarPairs(dictionary);
analyzeDynamicGrammarBits(dictionary);
analyzeOperandBitWidths(dictionary);
analyzeOperandBitPairs(dictionary);
analyzeOperandWidthWaste(dictionary);
analyzeOperandDistances(dictionary);
analyzeOperandDifference(dictionary);
analyzeOperandFrequency(dictionary);
analyzeOperandHuffman(dictionary);

std::vector<uint8_t> reachable;

markReachableRules(
    sequence,
    dictionary,
    reachable);

uint32_t reachableCount = 0;

for (uint8_t used : reachable) {
    if (used)
        ++reachableCount;
}

uint32_t unreachableCount =
    dictionary.size() -
    reachableCount;

std::printf(
    "\nGrammar pruning:\n"
    "created rules   : %u\n"
    "reachable rules : %u\n"
    "unreachable     : %u\n"
    "grammar before  : %llu bytes\n"
    "grammar after   : %llu bytes\n",
    dictionary.size(),
    reachableCount,
    unreachableCount,
    static_cast<unsigned long long>(
        dictionary.size() * 8ull),
    static_cast<unsigned long long>(
        reachableCount * 8ull));

    if (!writeGtc(
            outputName,
            input.size(),
            dictionary,
            sequence,
            stats)) {

        std::cerr
            << "error: cannot write "
            << outputName
            << "\n";

        return false;
    }
std::vector<uint32_t> uses;

countRuleUses(
    sequence,
    dictionary,
    uses);

uint32_t usedOnce = 0;
uint32_t usedTwice = 0;
uint32_t usedThreePlus = 0;
uint32_t maxUses = 0;

for (uint32_t count : uses) {
    if (count == 1)
        ++usedOnce;
    else if (count == 2)
        ++usedTwice;
    else if (count >= 3)
        ++usedThreePlus;

    if (count > maxUses)
        maxUses = count;
}

std::printf(
    "\nGrammar usage:\n"
    "used once       : %u\n"
    "used twice      : %u\n"
    "used 3+ times   : %u\n"
    "max uses        : %u\n",
    usedOnce,
    usedTwice,
    usedThreePlus,
    maxUses);
uint32_t onceExpansion2 = 0;
uint32_t onceExpansion3_4 = 0;
uint32_t onceExpansion5_8 = 0;
uint32_t onceExpansion9_16 = 0;
uint32_t onceExpansion17_32 = 0;
uint32_t onceExpansion33Plus = 0;

uint64_t onceExpansionTotal = 0;
uint64_t onceExpansionMax = 0;

std::vector<uint64_t> expansionCache(
    dictionary.size(),
    0);

for (uint32_t i = 0;
     i < dictionary.size();
     ++i) {

    if (uses[i] != 1)
        continue;

    uint32_t token =
        GTC_BASE_TOKENS + i;

    uint64_t expansion =
        ruleExpansionSize(
            token,
            dictionary,
            expansionCache);

    onceExpansionTotal += expansion;

    if (expansion > onceExpansionMax)
        onceExpansionMax = expansion;

    if (expansion == 2)
        ++onceExpansion2;
    else if (expansion <= 4)
        ++onceExpansion3_4;
    else if (expansion <= 8)
        ++onceExpansion5_8;
    else if (expansion <= 16)
        ++onceExpansion9_16;
    else if (expansion <= 32)
        ++onceExpansion17_32;
    else
        ++onceExpansion33Plus;
}

std::printf(
    "\nSingle-use rules:\n"
    "expanded 2       : %u\n"
    "expanded 3-4     : %u\n"
    "expanded 5-8     : %u\n"
    "expanded 9-16    : %u\n"
    "expanded 17-32   : %u\n"
    "expanded 33+     : %u\n"
    "total expansion  : %llu bytes\n"
    "maximum          : %llu bytes\n",
    onceExpansion2,
    onceExpansion3_4,
    onceExpansion5_8,
    onceExpansion9_16,
    onceExpansion17_32,
    onceExpansion33Plus,
    static_cast<unsigned long long>(
        onceExpansionTotal),
    static_cast<unsigned long long>(
        onceExpansionMax));
uint32_t chain1 = 0;
uint32_t chain2 = 0;
uint32_t chain3 = 0;
uint32_t chain4 = 0;
uint32_t chain5 = 0;
uint32_t chain6plus = 0;
uint32_t maxChain = 0;

for (uint32_t i = 0;
     i < dictionary.size();
     ++i) {

    if (uses[i] != 1)
        continue;

    uint32_t token =
        GTC_BASE_TOKENS + i;

    uint32_t depth =
        singleUseChainDepth(
            token,
            dictionary,
            uses);

    if (depth == 0)
        continue;

    if (depth == 1)
        ++chain1;
    else if (depth == 2)
        ++chain2;
    else if (depth == 3)
        ++chain3;
    else if (depth == 4)
        ++chain4;
    else if (depth == 5)
        ++chain5;
    else
        ++chain6plus;

    if (depth > maxChain)
        maxChain = depth;
}

std::printf(
    "\nSingle-use chains:\n"
    "depth 1         : %u\n"
    "depth 2         : %u\n"
    "depth 3         : %u\n"
    "depth 4         : %u\n"
    "depth 5         : %u\n"
    "depth 6+        : %u\n"
    "maximum depth   : %u\n",
    chain1,
    chain2,
    chain3,
    chain4,
    chain5,
    chain6plus,
    maxChain);
uint32_t subtree1 = 0;
uint32_t subtree2 = 0;
uint32_t subtree3 = 0;
uint32_t subtree4 = 0;
uint32_t subtree5 = 0;
uint32_t subtree6_8 = 0;
uint32_t subtree9_16 = 0;
uint32_t subtree17plus = 0;

uint32_t maxSubtree = 0;

uint64_t totalSubtreeRules = 0;
uint64_t potentialSavedBytes = 0;

for (uint32_t i = 0;
     i < dictionary.size();
     ++i) {

    if (uses[i] != 1)
        continue;

    uint32_t token =
        GTC_BASE_TOKENS + i;

    uint32_t size =
        singleUseSubtreeSize(
            token,
            dictionary,
            uses);

    if (size == 0)
        continue;

    if (size == 1)
        ++subtree1;
    else if (size == 2)
        ++subtree2;
    else if (size == 3)
        ++subtree3;
    else if (size == 4)
        ++subtree4;
    else if (size == 5)
        ++subtree5;
    else if (size <= 8)
        ++subtree6_8;
    else if (size <= 16)
        ++subtree9_16;
    else
        ++subtree17plus;

    if (size > maxSubtree)
        maxSubtree = size;

    totalSubtreeRules += size;

    /*
     * If the whole single-use subtree is inlined
     * into its root, all descendant rules disappear.
     *
     * The root rule itself remains.
     */
    if (size > 1) {
        potentialSavedBytes +=
            static_cast<uint64_t>(
                size - 1) * 8ull;
    }
}

std::printf(
    "\nSingle-use subtrees:\n"
    "size 1          : %u\n"
    "size 2          : %u\n"
    "size 3          : %u\n"
    "size 4          : %u\n"
    "size 5          : %u\n"
    "size 6-8        : %u\n"
    "size 9-16       : %u\n"
    "size 17+        : %u\n"
    "maximum size    : %u\n"
    "total subtree   : %llu rules\n"
    "potential saving: %llu bytes\n",
    subtree1,
    subtree2,
    subtree3,
    subtree4,
    subtree5,
    subtree6_8,
    subtree9_16,
    subtree17plus,
    maxSubtree,
    static_cast<unsigned long long>(
        totalSubtreeRules),
    static_cast<unsigned long long>(
        potentialSavedBytes));

    return true;
}
