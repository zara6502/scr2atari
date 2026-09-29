#include "gtc_table.h"
#include "gtc_encoder.h"
#include "gtc_format.h"
#include "gtc_huffman.h"

#include <cstdio>
#include <fstream>
#include <unordered_map>
#include <iostream>
#include <cstdlib>
#include <algorithm>
#include <iomanip>

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
uint32_t zigzag32(int32_t value)
{
    return
        (static_cast<uint32_t>(value) << 1) ^
        static_cast<uint32_t>(value >> 31);
}

void writeVarint(
    std::ofstream& out,
    uint32_t value)
{
    while (value >= 0x80) {
        const uint8_t b =
            static_cast<uint8_t>(value | 0x80);

        out.put(
            static_cast<char>(b));

        value >>= 7;
    }

    out.put(
        static_cast<char>(
            static_cast<uint8_t>(value)));
}
uint32_t varintSize(uint32_t value)
{
    uint32_t n = 1;

    while (value >= 128) {
        value >>= 7;
        ++n;
    }

    return n;
}
void writeGrammarValue(
    std::ofstream& out,
    uint16_t previous,
    uint16_t value)
{
    const int32_t delta =
        static_cast<int32_t>(value) -
        static_cast<int32_t>(previous);

    writeVarint(
        out,
        zigzag32(delta));
}
void printGrammarCompactStats(
    const char* title,
    const GtcDictionary& dictionary)
{
const uint32_t ruleCount =
    dictionary.size();

    const uint64_t rawBytes =
        static_cast<uint64_t>(ruleCount) * 4u;

    uint64_t totalBytes = 0;

    uint64_t leftBytes = 0;
    uint64_t rightBytes = 0;

    uint64_t leftOne = 0;
    uint64_t leftTwo = 0;
    uint64_t leftThreePlus = 0;

    uint64_t rightOne = 0;
    uint64_t rightTwo = 0;
    uint64_t rightThreePlus = 0;

    uint64_t leftAbsSum = 0;
    uint64_t rightAbsSum = 0;

    uint32_t leftMaxAbs = 0;
    uint32_t rightMaxAbs = 0;

    uint16_t previousLeft = 0;
    uint16_t previousRight = 0;

    for (uint32_t i = 0; i < ruleCount; ++i) {
        const GtcRule& rule =
            dictionary.rule(
                GTC_BASE_TOKENS + i);

        const int32_t leftDelta =
            static_cast<int32_t>(rule.left) -
            static_cast<int32_t>(previousLeft);

        const int32_t rightDelta =
            static_cast<int32_t>(rule.right) -
            static_cast<int32_t>(previousRight);

        const uint32_t leftZigzag =
            zigzag32(leftDelta);

        const uint32_t rightZigzag =
            zigzag32(rightDelta);

        const uint32_t lb =
            varintSize(leftZigzag);

        const uint32_t rb =
            varintSize(rightZigzag);

        leftBytes += lb;
        rightBytes += rb;

        totalBytes += lb + rb;

        leftAbsSum +=
            static_cast<uint64_t>(
                std::llabs(
                    static_cast<long long>(leftDelta)));

        rightAbsSum +=
            static_cast<uint64_t>(
                std::llabs(
                    static_cast<long long>(rightDelta)));

        leftMaxAbs = std::max(
            leftMaxAbs,
            static_cast<uint32_t>(
                std::llabs(
                    static_cast<long long>(leftDelta))));

        rightMaxAbs = std::max(
            rightMaxAbs,
            static_cast<uint32_t>(
                std::llabs(
                    static_cast<long long>(rightDelta))));

        if (lb == 1)
            ++leftOne;
        else if (lb == 2)
            ++leftTwo;
        else
            ++leftThreePlus;

        if (rb == 1)
            ++rightOne;
        else if (rb == 2)
            ++rightTwo;
        else
            ++rightThreePlus;

        previousLeft = rule.left;
        previousRight = rule.right;
    }

    std::cout
        << "\n"
        << title
        << "\n"
        << "============================\n";

    std::cout
        << "rules             : "
        << ruleCount << "\n";

    std::cout
        << "raw grammar       : "
        << rawBytes << " bytes\n";

    std::cout
        << "compact grammar   : "
        << totalBytes << " bytes\n";

    if (rawBytes != 0) {
        const double saving =
            100.0 *
            (1.0 -
             static_cast<double>(totalBytes) /
             static_cast<double>(rawBytes));

        std::cout
            << "saving            : "
            << std::fixed
            << std::setprecision(2)
            << saving
            << "%\n";
    }

    std::cout
        << "\nLEFT\n";

    std::cout
        << "1 byte            : "
        << leftOne << "\n"
        << "2 bytes           : "
        << leftTwo << "\n"
        << "3+ bytes          : "
        << leftThreePlus << "\n"
        << "average abs delta : "
        << (ruleCount
            ? static_cast<double>(leftAbsSum) /
              static_cast<double>(ruleCount)
            : 0.0)
        << "\n"
        << "max abs delta     : "
        << leftMaxAbs << "\n"
        << "encoded bytes     : "
        << leftBytes << "\n";

    std::cout
        << "\nRIGHT\n";

    std::cout
        << "1 byte            : "
        << rightOne << "\n"
        << "2 bytes           : "
        << rightTwo << "\n"
        << "3+ bytes          : "
        << rightThreePlus << "\n"
        << "average abs delta : "
        << (ruleCount
            ? static_cast<double>(rightAbsSum) /
              static_cast<double>(ruleCount)
            : 0.0)
        << "\n"
        << "max abs delta     : "
        << rightMaxAbs << "\n"
        << "encoded bytes     : "
        << rightBytes << "\n";
}
void printGrammarDistanceStats(
    const char* title,
    const GtcDictionary& dictionary)
{
    const uint32_t count =
        dictionary.size();

    uint64_t leftBytes = 0;
    uint64_t rightBytes = 0;

    uint64_t leftCount[5] = {};
    uint64_t rightCount[5] = {};

    uint64_t leftLiteralBytes = 0;
    uint64_t rightLiteralBytes = 0;

    uint64_t leftRefBytes = 0;
    uint64_t rightRefBytes = 0;

    uint32_t leftMax = 0;
    uint32_t rightMax = 0;

    uint64_t leftRefs = 0;
    uint64_t rightRefs = 0;

    uint64_t leftLiterals = 0;
    uint64_t rightLiterals = 0;

    for (uint32_t i = 0;
         i < count;
         ++i) {

        const GtcRule& rule =
            dictionary.rule(
                256u + i);

        const uint32_t currentToken =
            256u + i;

        /*
         * left
         */
        if (rule.left < 256u) {

            ++leftLiterals;

            leftLiteralBytes += 1;
        }
        else {

            ++leftRefs;

            const uint32_t distance =
                currentToken -
                static_cast<uint32_t>(
                    rule.left);

            const uint32_t bytes =
                varintSize(distance);

            leftBytes += bytes;
            leftRefBytes += bytes;

            ++leftCount[
                bytes >= 4 ? 4 : bytes];

            if (distance > leftMax)
                leftMax = distance;
        }

        /*
         * right
         */
        if (rule.right < 256u) {

            ++rightLiterals;

            rightLiteralBytes += 1;
        }
        else {

            ++rightRefs;

            const uint32_t distance =
                currentToken -
                static_cast<uint32_t>(
                    rule.right);

            const uint32_t bytes =
                varintSize(distance);

            rightBytes += bytes;
            rightRefBytes += bytes;

            ++rightCount[
                bytes >= 4 ? 4 : bytes];

            if (distance > rightMax)
                rightMax = distance;
        }
    }

    const uint64_t totalBytes =
        leftLiteralBytes +
        rightLiteralBytes +
        leftRefBytes +
        rightRefBytes;

    const uint64_t rawBytes =
        static_cast<uint64_t>(count) * 4u;

    const double saving =
        rawBytes == 0
            ? 0.0
            : 100.0 *
              (1.0 -
               static_cast<double>(totalBytes) /
               static_cast<double>(rawBytes));

    std::printf(
        "\n"
        "%s\n"
        "========================================\n"
        "rules                 : %u\n"
        "\n"
        "BACK-DISTANCE GRAMMAR\n"
        "  total               : %llu bytes\n"
        "  saving              : %.2f%%\n"
        "\n"
        "LEFT\n"
        "  literals            : %llu\n"
        "  references          : %llu\n"
        "  literal bytes       : %llu\n"
        "  reference bytes     : %llu\n"
        "  total bytes         : %llu\n"
        "  1 byte refs         : %llu\n"
        "  2 byte refs         : %llu\n"
        "  3 byte refs         : %llu\n"
        "  4+ byte refs        : %llu\n"
        "  max distance        : %u\n"
        "\n"
        "RIGHT\n"
        "  literals            : %llu\n"
        "  references          : %llu\n"
        "  literal bytes       : %llu\n"
        "  reference bytes     : %llu\n"
        "  total bytes         : %llu\n"
        "  1 byte refs         : %llu\n"
        "  2 byte refs         : %llu\n"
        "  3 byte refs         : %llu\n"
        "  4+ byte refs        : %llu\n"
        "  max distance        : %u\n",
        title,
        count,

        static_cast<unsigned long long>(
            totalBytes),

        saving,

        static_cast<unsigned long long>(
            leftLiterals),

        static_cast<unsigned long long>(
            leftRefs),

        static_cast<unsigned long long>(
            leftLiteralBytes),

        static_cast<unsigned long long>(
            leftRefBytes),

        static_cast<unsigned long long>(
            leftLiteralBytes +
            leftRefBytes),

        static_cast<unsigned long long>(
            leftCount[1]),

        static_cast<unsigned long long>(
            leftCount[2]),

        static_cast<unsigned long long>(
            leftCount[3]),

        static_cast<unsigned long long>(
            leftCount[4]),

        leftMax,

        static_cast<unsigned long long>(
            rightLiterals),

        static_cast<unsigned long long>(
            rightRefs),

        static_cast<unsigned long long>(
            rightLiteralBytes),

        static_cast<unsigned long long>(
            rightRefBytes),

        static_cast<unsigned long long>(
            rightLiteralBytes +
            rightRefBytes),

        static_cast<unsigned long long>(
            rightCount[1]),

        static_cast<unsigned long long>(
            rightCount[2]),

        static_cast<unsigned long long>(
            rightCount[3]),

        static_cast<unsigned long long>(
            rightCount[4]),

        rightMax);
}
void printGrammarUsageStats(
    const char* title,
    const GtcDictionary& dictionary,
    const std::vector<uint32_t>& sequence)
{
    const uint32_t count =
        dictionary.size();

    if (count == 0)
        return;

    std::vector<uint64_t> directUse(count, 0);
    std::vector<uint64_t> grammarUse(count, 0);

    /*
     * Count rules that actually occur in the
     * final token stream.
     */
    for (uint32_t token : sequence) {

        if (token >= GTC_BASE_TOKENS &&
            token < GTC_BASE_TOKENS + count) {

            ++directUse[
                token - GTC_BASE_TOKENS];
        }
    }

    /*
     * Count references from other grammar rules.
     */
    for (uint32_t i = 0;
         i < count;
         ++i) {

        const GtcRule& rule =
            dictionary.rule(
                GTC_BASE_TOKENS + i);

        if (rule.left >= GTC_BASE_TOKENS &&
            rule.left <
                GTC_BASE_TOKENS + count) {

            ++grammarUse[
                rule.left - GTC_BASE_TOKENS];
        }

        if (rule.right >= GTC_BASE_TOKENS &&
            rule.right <
                GTC_BASE_TOKENS + count) {

            ++grammarUse[
                rule.right - GTC_BASE_TOKENS];
        }
    }

    uint64_t directRules = 0;
    uint64_t intermediateRules = 0;
    uint64_t unusedRules = 0;

    uint64_t directTokens = 0;

    uint64_t maxGrammarUse = 0;
    uint32_t maxGrammarUseRule = 0;

    for (uint32_t i = 0;
         i < count;
         ++i) {

        if (directUse[i] > 0)
            ++directRules;

        if (directUse[i] == 0 &&
            grammarUse[i] > 0)
            ++intermediateRules;

        if (directUse[i] == 0 &&
            grammarUse[i] == 0)
            ++unusedRules;

        directTokens += directUse[i];

        if (grammarUse[i] > maxGrammarUse) {

            maxGrammarUse =
                grammarUse[i];

            maxGrammarUseRule = i;
        }
    }

    /*
     * Calculate dependency depth.
     *
     * Literal/reference:
     *
     * depth(literal) = 0
     * depth(rule)   = 1 + max(depth(left), depth(right))
     */
    std::vector<uint32_t> depth(count, 0);

    uint32_t maxDepth = 0;
    uint32_t maxDepthRule = 0;

    uint64_t totalDepth = 0;

    for (uint32_t i = 0;
         i < count;
         ++i) {

        const GtcRule& rule =
            dictionary.rule(
                GTC_BASE_TOKENS + i);

        uint32_t leftDepth = 0;
        uint32_t rightDepth = 0;

        if (rule.left >= GTC_BASE_TOKENS) {

            const uint32_t dependency =
                static_cast<uint32_t>(
                    rule.left) -
                GTC_BASE_TOKENS;

            if (dependency < i)
                leftDepth =
                    depth[dependency] + 1;
        }

        if (rule.right >= GTC_BASE_TOKENS) {

            const uint32_t dependency =
                static_cast<uint32_t>(
                    rule.right) -
                GTC_BASE_TOKENS;

            if (dependency < i)
                rightDepth =
                    depth[dependency] + 1;
        }

        depth[i] =
            leftDepth > rightDepth
                ? leftDepth
                : rightDepth;

        totalDepth += depth[i];

        if (depth[i] > maxDepth) {

            maxDepth =
                depth[i];

            maxDepthRule = i;
        }
    }

    const double directPercent =
        100.0 *
        static_cast<double>(directRules) /
        static_cast<double>(count);

    const double intermediatePercent =
        100.0 *
        static_cast<double>(intermediateRules) /
        static_cast<double>(count);

    const double unusedPercent =
        100.0 *
        static_cast<double>(unusedRules) /
        static_cast<double>(count);

    const double averageDepth =
        static_cast<double>(totalDepth) /
        static_cast<double>(count);

    std::printf(
        "\n"
        "%s\n"
        "========================================\n"
        "rules                 : %u\n"
        "\n"
        "RULE USAGE\n"
        "  direct rules        : %llu (%.2f%%)\n"
        "  intermediate rules  : %llu (%.2f%%)\n"
        "  unused rules        : %llu (%.2f%%)\n"
        "\n"
        "FINAL TOKEN STREAM\n"
        "  tokens              : %llu\n"
        "  direct rule tokens  : %llu\n"
        "\n"
        "GRAMMAR REFERENCES\n"
        "  total references    : %llu\n"
        "  max refs to rule    : %llu\n"
        "  max refs rule       : R%u\n"
        "\n"
        "DEPENDENCY DEPTH\n"
        "  average depth       : %.3f\n"
        "  max depth           : %u\n"
        "  max depth rule      : R%u\n",
        title,
        count,

        static_cast<unsigned long long>(
            directRules),

        directPercent,

        static_cast<unsigned long long>(
            intermediateRules),

        intermediatePercent,

        static_cast<unsigned long long>(
            unusedRules),

        unusedPercent,

        static_cast<unsigned long long>(
            sequence.size()),

        static_cast<unsigned long long>(
            directTokens),

        static_cast<unsigned long long>(
            [&]() -> uint64_t {
                uint64_t total = 0;

                for (uint32_t i = 0;
                     i < count;
                     ++i) {

                    total += grammarUse[i];
                }

                return total;
            }()),

        static_cast<unsigned long long>(
            maxGrammarUse),

        maxGrammarUseRule,

        averageDepth,

        maxDepth,

        maxDepthRule);
}
void printGrammarExpandedStats(
    const char* title,
    const GtcDictionary& dictionary,
    const std::vector<uint32_t>& sequence)
{
    const uint32_t count =
        dictionary.size();

    if (count == 0)
        return;

    /*
     * expandedSize[i] =
     * number of base tokens represented
     * by grammar rule i.
     *
     * Literal = 1
     * Rule    = expanded(left) + expanded(right)
     */
    std::vector<uint64_t> expandedSize(
        count, 0);

    std::vector<uint64_t> directUse(
        count, 0);

    std::vector<uint64_t> grammarUse(
        count, 0);

    /*
     * Count direct uses in final stream.
     */
    for (uint32_t token : sequence) {

        if (token >= GTC_BASE_TOKENS &&
            token <
                GTC_BASE_TOKENS + count) {

            ++directUse[
                token - GTC_BASE_TOKENS];
        }
    }

    /*
     * Count references from grammar rules.
     */
    for (uint32_t i = 0;
         i < count;
         ++i) {

        const GtcRule& rule =
            dictionary.rule(
                GTC_BASE_TOKENS + i);

        if (rule.left >= GTC_BASE_TOKENS &&
            rule.left <
                GTC_BASE_TOKENS + count) {

            ++grammarUse[
                rule.left - GTC_BASE_TOKENS];
        }

        if (rule.right >= GTC_BASE_TOKENS &&
            rule.right <
                GTC_BASE_TOKENS + count) {

            ++grammarUse[
                rule.right - GTC_BASE_TOKENS];
        }
    }

    /*
     * Dictionary is already topologically ordered
     * by reorderForEncoding(), so dependencies always
     * point to an earlier rule.
     */
    for (uint32_t i = 0;
         i < count;
         ++i) {

        const GtcRule& rule =
            dictionary.rule(
                GTC_BASE_TOKENS + i);

        uint64_t leftSize = 1;
        uint64_t rightSize = 1;

        if (rule.left >= GTC_BASE_TOKENS) {

            const uint32_t dependency =
                static_cast<uint32_t>(
                    rule.left) -
                GTC_BASE_TOKENS;

            if (dependency < i)
                leftSize =
                    expandedSize[dependency];
        }

        if (rule.right >= GTC_BASE_TOKENS) {

            const uint32_t dependency =
                static_cast<uint32_t>(
                    rule.right) -
                GTC_BASE_TOKENS;

            if (dependency < i)
                rightSize =
                    expandedSize[dependency];
        }

        expandedSize[i] =
            leftSize + rightSize;
    }

    uint64_t minSize =
        UINT64_MAX;

    uint64_t maxSize = 0;
    uint64_t totalSize = 0;

    uint32_t minRule = 0;
    uint32_t maxRule = 0;

    uint64_t totalDirectCoverage = 0;
    uint64_t totalGrammarCoverage = 0;

    /*
     * Find largest/smallest rules and calculate
     * coverage.
     */
    for (uint32_t i = 0;
         i < count;
         ++i) {

        const uint64_t size =
            expandedSize[i];

        if (size < minSize) {

            minSize = size;
            minRule = i;
        }

        if (size > maxSize) {

            maxSize = size;
            maxRule = i;
        }

        totalSize += size;

        /*
         * How many base tokens are represented
         * by direct final-stream uses?
         */
        totalDirectCoverage +=
            directUse[i] * size;

        /*
         * How many base tokens are represented
         * through grammar references?
         *
         * This is structural coverage, not unique
         * coverage. A deeply nested rule may therefore
         * contribute multiple times.
         */
        totalGrammarCoverage +=
            grammarUse[i] * size;
    }

    /*
     * Distribution.
     */
    uint64_t size1 = 0;
    uint64_t size2 = 0;
    uint64_t size3_4 = 0;
    uint64_t size5_8 = 0;
    uint64_t size9_16 = 0;
    uint64_t size17_32 = 0;
    uint64_t size33_64 = 0;
    uint64_t size65_128 = 0;
    uint64_t size129_256 = 0;
    uint64_t size257_512 = 0;
    uint64_t size513_plus = 0;

    for (uint32_t i = 0;
         i < count;
         ++i) {

        const uint64_t size =
            expandedSize[i];

        if (size == 1)
            ++size1;
        else if (size == 2)
            ++size2;
        else if (size <= 4)
            ++size3_4;
        else if (size <= 8)
            ++size5_8;
        else if (size <= 16)
            ++size9_16;
        else if (size <= 32)
            ++size17_32;
        else if (size <= 64)
            ++size33_64;
        else if (size <= 128)
            ++size65_128;
        else if (size <= 256)
            ++size129_256;
        else if (size <= 512)
            ++size257_512;
        else
            ++size513_plus;
    }

    const double averageSize =
        static_cast<double>(totalSize) /
        static_cast<double>(count);

    /*
     * Calculate actual expanded size of the
     * final token stream.
     */
    uint64_t finalExpandedSize = 0;

    for (uint32_t token : sequence) {

        if (token >= GTC_BASE_TOKENS &&
            token <
                GTC_BASE_TOKENS + count) {

            finalExpandedSize +=
                expandedSize[
                    token - GTC_BASE_TOKENS];
        }
        else {
            ++finalExpandedSize;
        }
    }

    std::printf(
        "\n"
        "%s\n"
        "========================================\n"
        "rules                 : %u\n"
        "\n"
        "EXPANDED SIZE\n"
        "  minimum             : %llu tokens (R%u)\n"
        "  maximum             : %llu tokens (R%u)\n"
        "  average             : %.2f tokens\n"
        "  final stream        : %llu tokens\n"
        "\n"
        "RULE SIZE DISTRIBUTION\n"
        "  1                   : %llu\n"
        "  2                   : %llu\n"
        "  3-4                 : %llu\n"
        "  5-8                 : %llu\n"
        "  9-16                : %llu\n"
        "  17-32               : %llu\n"
        "  33-64               : %llu\n"
        "  65-128              : %llu\n"
        "  129-256             : %llu\n"
        "  257-512             : %llu\n"
        "  513+                : %llu\n"
        "\n"
        "COVERAGE\n"
        "  direct coverage     : %llu base tokens\n"
        "  grammar coverage    : %llu base tokens\n",
        title,
        count,

        static_cast<unsigned long long>(
            minSize),

        minRule,

        static_cast<unsigned long long>(
            maxSize),

        maxRule,

        averageSize,

        static_cast<unsigned long long>(
            finalExpandedSize),

        static_cast<unsigned long long>(
            size1),

        static_cast<unsigned long long>(
            size2),

        static_cast<unsigned long long>(
            size3_4),

        static_cast<unsigned long long>(
            size5_8),

        static_cast<unsigned long long>(
            size9_16),

        static_cast<unsigned long long>(
            size17_32),

        static_cast<unsigned long long>(
            size33_64),

        static_cast<unsigned long long>(
            size65_128),

        static_cast<unsigned long long>(
            size129_256),

        static_cast<unsigned long long>(
            size257_512),

        static_cast<unsigned long long>(
            size513_plus),

        static_cast<unsigned long long>(
            totalDirectCoverage),

        static_cast<unsigned long long>(
            totalGrammarCoverage));
}
void printGrammarValueStats(
    const GtcDictionary& dictionary,
    const std::vector<uint32_t>& sequence)
{
    struct RuleStats {
        uint32_t index;

        uint64_t directUse;
        uint64_t grammarUse;
        uint64_t totalUse;

        uint64_t expandedSize;

        uint64_t directCoverage;
        uint64_t totalCoverage;

        uint64_t directGain;
        uint64_t totalGain;

        uint32_t grammarCost;
        int64_t netGain;
    };

    const uint32_t ruleCount =
        dictionary.size();

    if (ruleCount == 0)
        return;

    std::vector<uint64_t> directUse(
        ruleCount, 0);

    std::vector<uint64_t> grammarUse(
        ruleCount, 0);

    std::vector<uint64_t> expandedSize(
        ruleCount, 0);

    /*
     * --------------------------------------------------
     * Expanded size of every rule.
     *
     * Literal = 1 base token.
     * Rule    = expanded(left) + expanded(right).
     *
     * Dictionary is already topologically reordered.
     * --------------------------------------------------
     */
    for (uint32_t i = 0;
         i < ruleCount;
         ++i) {

        const GtcRule& rule =
            dictionary.rule(
                GTC_BASE_TOKENS + i);

        uint64_t leftSize = 1;
        uint64_t rightSize = 1;

        if (rule.left >= GTC_BASE_TOKENS) {

            const uint32_t ref =
                static_cast<uint32_t>(
                    rule.left) -
                GTC_BASE_TOKENS;

            if (ref < i)
                leftSize =
                    expandedSize[ref];
        }

        if (rule.right >= GTC_BASE_TOKENS) {

            const uint32_t ref =
                static_cast<uint32_t>(
                    rule.right) -
                GTC_BASE_TOKENS;

            if (ref < i)
                rightSize =
                    expandedSize[ref];
        }

        expandedSize[i] =
            leftSize + rightSize;
    }

    /*
     * --------------------------------------------------
     * Direct uses in final token stream.
     * --------------------------------------------------
     */
    for (uint32_t token : sequence) {

        if (token < GTC_BASE_TOKENS)
            continue;

        const uint32_t index =
            token - GTC_BASE_TOKENS;

        if (index < ruleCount)
            ++directUse[index];
    }

    /*
     * --------------------------------------------------
     * References from other grammar rules.
     * --------------------------------------------------
     */
    for (uint32_t i = 0;
         i < ruleCount;
         ++i) {

        const GtcRule& rule =
            dictionary.rule(
                GTC_BASE_TOKENS + i);

        if (rule.left >= GTC_BASE_TOKENS) {

            const uint32_t index =
                static_cast<uint32_t>(
                    rule.left) -
                GTC_BASE_TOKENS;

            if (index < ruleCount)
                ++grammarUse[index];
        }

        if (rule.right >= GTC_BASE_TOKENS) {

            const uint32_t index =
                static_cast<uint32_t>(
                    rule.right) -
                GTC_BASE_TOKENS;

            if (index < ruleCount)
                ++grammarUse[index];
        }
    }

    /*
     * --------------------------------------------------
     * Build statistics.
     * --------------------------------------------------
     */
    std::vector<RuleStats> stats;

    stats.reserve(ruleCount);

    uint16_t previousLeft = 0;
    uint16_t previousRight = 0;

    for (uint32_t i = 0;
         i < ruleCount;
         ++i) {

        const GtcRule& rule =
            dictionary.rule(
                GTC_BASE_TOKENS + i);

        const uint64_t totalUse =
            directUse[i] +
            grammarUse[i];

        RuleStats s{};

        s.index =
            i;

        s.directUse =
            directUse[i];

        s.grammarUse =
            grammarUse[i];

        s.totalUse =
            totalUse;

        s.expandedSize =
            expandedSize[i];

        /*
         * Coverage.
         */
        s.directCoverage =
            directUse[i] *
            expandedSize[i];

        s.totalCoverage =
            totalUse *
            expandedSize[i];

        /*
         * Base-token saving caused by replacing
         * a rule occurrence with one token.
         *
         * Example:
         *
         * rule expands to 6 bytes
         * one rule token replaces those 6 bytes
         *
         * gain = 6 - 1 = 5
         */
        const uint64_t gainPerUse =
            expandedSize[i] > 0
                ? expandedSize[i] - 1
                : 0;

        s.directGain =
            directUse[i] *
            gainPerUse;

        s.totalGain =
            totalUse *
            gainPerUse;

        /*
         * --------------------------------------------------
         * Actual compact grammar storage cost of this rule.
         *
         * Values are encoded as:
         *
         *   delta from previous rule
         *   ZigZag
         *   VarInt
         *
         * Two values per rule.
         * --------------------------------------------------
         */
        const int32_t leftDelta =
            static_cast<int32_t>(
                rule.left) -
            static_cast<int32_t>(
                previousLeft);

        const int32_t rightDelta =
            static_cast<int32_t>(
                rule.right) -
            static_cast<int32_t>(
                previousRight);

        s.grammarCost =
            varintSize(
                zigzag32(leftDelta)) +
            varintSize(
                zigzag32(rightDelta));

        /*
         * Very simple diagnostic net gain:
         *
         * total expanded-token saving
         * minus this rule's compact grammar cost.
         *
         * IMPORTANT:
         *
         * This is not the final compressed-size gain.
         * It is only a structural diagnostic.
         */
        s.netGain =
            static_cast<int64_t>(
                s.totalGain) -
            static_cast<int64_t>(
                s.grammarCost);

        stats.push_back(
            s);

        previousLeft =
            rule.left;

        previousRight =
            rule.right;
    }

    const size_t topCount =
        std::min<size_t>(
            30,
            stats.size());

    std::cout
        << "\n"
        << "GRAMMAR VALUE STATS\n"
        << "===================\n"
        << "rules                 : "
        << ruleCount
        << "\n";

    /*
     * --------------------------------------------------
     * TOP BY DIRECT COVERAGE
     * --------------------------------------------------
     */
    std::sort(
        stats.begin(),
        stats.end(),
        [](const RuleStats& a,
           const RuleStats& b) {

            if (a.directCoverage !=
                b.directCoverage)
                return
                    a.directCoverage >
                    b.directCoverage;

            return
                a.index <
                b.index;
        });

    std::cout
        << "\n"
        << "TOP RULES BY DIRECT COVERAGE\n"
        << "\n";

    for (size_t n = 0;
         n < topCount;
         ++n) {

        const RuleStats& s =
            stats[n];

        std::cout
            << "R" << s.index
            << "  size=" << s.expandedSize
            << "  direct=" << s.directUse
            << "  internal=" << s.grammarUse
            << "  total=" << s.totalUse
            << "  direct*size=" << s.directCoverage
            << "  total*size=" << s.totalCoverage
            << "  directGain=" << s.directGain
            << "  totalGain=" << s.totalGain
            << "  grammarCost=" << s.grammarCost
            << "  netGain=" << s.netGain
            << "\n";
    }

    /*
     * --------------------------------------------------
     * TOP BY DIRECT GAIN
     * --------------------------------------------------
     */
    std::sort(
        stats.begin(),
        stats.end(),
        [](const RuleStats& a,
           const RuleStats& b) {

            if (a.directGain !=
                b.directGain)
                return
                    a.directGain >
                    b.directGain;

            return
                a.index <
                b.index;
        });

    std::cout
        << "\n"
        << "TOP RULES BY DIRECT GAIN\n"
        << "\n";

    for (size_t n = 0;
         n < topCount;
         ++n) {

        const RuleStats& s =
            stats[n];

        std::cout
            << "R" << s.index
            << "  size=" << s.expandedSize
            << "  direct=" << s.directUse
            << "  internal=" << s.grammarUse
            << "  directGain=" << s.directGain
            << "  totalGain=" << s.totalGain
            << "  grammarCost=" << s.grammarCost
            << "  netGain=" << s.netGain
            << "\n";
    }

    /*
     * --------------------------------------------------
     * TOP BY NET GAIN
     * --------------------------------------------------
     */
    std::sort(
        stats.begin(),
        stats.end(),
        [](const RuleStats& a,
           const RuleStats& b) {

            if (a.netGain !=
                b.netGain)
                return
                    a.netGain >
                    b.netGain;

            return
                a.index <
                b.index;
        });

    std::cout
        << "\n"
        << "TOP RULES BY NET GAIN\n"
        << "\n";

    for (size_t n = 0;
         n < topCount;
         ++n) {

        const RuleStats& s =
            stats[n];

        std::cout
            << "R" << s.index
            << "  size=" << s.expandedSize
            << "  direct=" << s.directUse
            << "  internal=" << s.grammarUse
            << "  total=" << s.totalUse
            << "  totalGain=" << s.totalGain
            << "  grammarCost=" << s.grammarCost
            << "  netGain=" << s.netGain
            << "\n";
    }

    /*
     * --------------------------------------------------
     * TOP BY EXPANDED SIZE
     * --------------------------------------------------
     */
    std::sort(
        stats.begin(),
        stats.end(),
        [](const RuleStats& a,
           const RuleStats& b) {

            if (a.expandedSize !=
                b.expandedSize)
                return
                    a.expandedSize >
                    b.expandedSize;

            return
                a.index <
                b.index;
        });

    std::cout
        << "\n"
        << "TOP RULES BY EXPANDED SIZE\n"
        << "\n";

    for (size_t n = 0;
         n < topCount;
         ++n) {

        const RuleStats& s =
            stats[n];

        std::cout
            << "R" << s.index
            << "  size=" << s.expandedSize
            << "  direct=" << s.directUse
            << "  internal=" << s.grammarUse
            << "  total=" << s.totalUse
            << "  direct*size=" << s.directCoverage
            << "  total*size=" << s.totalCoverage
            << "  directGain=" << s.directGain
            << "  totalGain=" << s.totalGain
            << "\n";
    }
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
        if (best.count < 5) break;

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
 * Compact grammar.
 *
 * Rules are already reordered by DEPS+LEFT.
 *
 * Each value is stored as a signed delta from
 * the corresponding value of the previous rule,
 * encoded with ZigZag + VarInt.
 */
uint16_t previousLeft = 0;
uint16_t previousRight = 0;

for (uint32_t i = 0;
     i < dictionary.size();
     ++i) {

    const GtcRule& rule =
        dictionary.rule(256u + i);

    writeGrammarValue(
        out,
        previousLeft,
        rule.left);

    writeGrammarValue(
        out,
        previousRight,
        rule.right);

    previousLeft = rule.left;
    previousRight = rule.right;
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

bool GtcEncoder::writeArchive( const std::string& filename, const std::vector<GtcArchiveFile>& files, const GtcDictionary& dictionary, const std::vector<uint32_t>& sequence, bool perFileHuffman, GtcArchiveStats& stats) { const uint32_t tokenCount = GTC_BASE_TOKENS + dictionary.size(); /* * The sequence still contains internal file * boundary markers. Remove them while creating * the final token stream. */ std::vector<uint32_t> tokens; tokens.reserve( sequence.size()); for (uint32_t token : sequence) { if (token == GTC_FILE_BOUNDARY) continue; if (token >= tokenCount) return false; tokens.push_back(token); } /* * Number of Huffman models stored in the archive. * * Normal mode: * * one global model * * Experimental mode: * * one model per file */ const uint32_t huffmanModelCount = perFileHuffman ? static_cast<uint32_t>(files.size()) : 1u; if (huffmanModelCount == 0) return false; /* * Build Huffman models. */ std::vector<GtcHuffman> huffmans; huffmans.resize( huffmanModelCount); if (!perFileHuffman) { /* * One global Huffman model. */ std::vector<uint64_t> frequencies( tokenCount, 0); for (uint32_t token : tokens) ++frequencies[token]; if (!huffmans[0].build( frequencies)) { return false; } } else { /* * One independent Huffman model * for each file. */ size_t tokenPosition = 0; for (size_t fileIndex = 0; fileIndex < files.size(); ++fileIndex) { const GtcArchiveFile& entry = files[fileIndex]; if (entry.tokenCount > tokens.size() - tokenPosition) { return false; } std::vector<uint64_t> frequencies( tokenCount, 0); for (uint64_t i = 0; i < entry.tokenCount; ++i) { ++frequencies[ tokens[tokenPosition + i]]; } if (!huffmans[fileIndex].build( frequencies)) { return false; } tokenPosition += static_cast<size_t>( entry.tokenCount); } if (tokenPosition != tokens.size()) { return false; } } /* * Encode the complete token stream. * * Every file starts at a precisely recorded * bit offset. */ BitWriter writer; std::vector<GtcArchiveFile> index = files; size_t tokenPosition = 0; for (size_t fileIndex = 0; fileIndex < index.size(); ++fileIndex) { GtcArchiveFile& entry = index[fileIndex]; entry.tokenStart = tokenPosition; entry.bitOffset = writer.bitCount(); if (entry.tokenCount > tokens.size() - tokenPosition) { return false; } const size_t count = static_cast<size_t>( entry.tokenCount); std::vector<uint32_t> fileTokens; fileTokens.reserve(count); for (size_t i = 0; i < count; ++i) { fileTokens.push_back( tokens[tokenPosition + i]); } /* * Normal mode: * * huffmans[0] * * Per-file mode: * * huffmans[fileIndex] */ const GtcHuffman& huffman = perFileHuffman ? huffmans[fileIndex] : huffmans[0]; huffman.encode( fileTokens, writer); tokenPosition += count; } if (tokenPosition != tokens.size()) { return false; } writer.flush(); const std::vector<uint8_t>& compressed = writer.data(); /* * Encode all Huffman code-length tables. * * There is one table for the global mode, * or one table per file in per-file mode. */ std::vector< std::vector<uint8_t>> tableData; tableData.resize( huffmanModelCount); GtcLengthTable table; for (uint32_t modelIndex = 0; modelIndex < huffmanModelCount; ++modelIndex) { GtcTableStats tableStats; if (!table.encode( huffmans[modelIndex].codeLengths(), tableData[modelIndex], tableStats)) { return false; } } /* * Build the filename table. */ std::vector<uint8_t> names; for (const GtcArchiveFile& entry : index) { names.insert( names.end(), entry.name.begin(), entry.name.end()); names.push_back(0); } /* * Archive index entry: * * nameOffset u32 * nameLength u32 * originalSize u64 * tokenStart u64 * tokenCount u64 * bitOffset u64 * * 40 bytes per file. */ const uint32_t indexEntrySize = 40; const uint64_t indexSize64 = static_cast<uint64_t>( index.size()) * indexEntrySize; if (indexSize64 > UINT32_MAX || names.size() > UINT32_MAX) { return false; } std::ofstream out( filename, std::ios::binary); if (!out) return false; GtcArchiveHeader header{}; header.magic = GTC_MAGIC; header.version = GTC_ARCHIVE_VERSION; for (const GtcArchiveFile& entry : index) header.original_size += entry.originalSize; header.token_count = tokenCount; header.grammar_count = dictionary.size(); header.token_count_in_stream = tokens.size(); header.compressed_size = compressed.size(); header.file_count = static_cast<uint32_t>( index.size()); header.index_size = static_cast<uint32_t>( indexSize64); header.names_size = static_cast<uint32_t>( names.size()); header.huffman_model_count = huffmanModelCount; /* * Header. */ writeU32( out, header.magic); writeU32( out, header.version); writeU64( out, header.original_size); writeU32( out, header.token_count); writeU32( out, header.grammar_count); writeU64( out, header.token_count_in_stream); writeU64( out, header.compressed_size); writeU32( out, header.file_count); writeU32( out, header.index_size); writeU32( out, header.names_size); writeU32( out, header.huffman_model_count); /* * Index. */ uint32_t nameOffset = 0; for (const GtcArchiveFile& entry : index) { writeU32( out, nameOffset); writeU32( out, static_cast<uint32_t>( entry.name.size())); writeU64( out, entry.originalSize); writeU64( out, entry.tokenStart); writeU64( out, entry.tokenCount); writeU64( out, entry.bitOffset); nameOffset += static_cast<uint32_t>( entry.name.size() + 1); } /* * Names. */ if (!names.empty()) { out.write( reinterpret_cast<const char*>( names.data()), static_cast<std::streamsize>( names.size())); } /* * Global grammar. */ for (uint32_t i = 0; i < dictionary.size(); ++i) { const GtcRule& rule = dictionary.rule( 256u + i); writeU16( out, rule.left); writeU16( out, rule.right); } /* * Huffman code-length tables. * * Format: * * u32 tableSize * tableData * * repeated huffman_model_count times. */
uint64_t totalTableBytes = 0;

for (uint32_t modelIndex = 0;
     modelIndex < huffmanModelCount;
     ++modelIndex) {

    const std::vector<uint8_t>& data =
        tableData[modelIndex];

    std::cerr
        << "Huffman table "
        << modelIndex
        << ": "
        << data.size()
        << " bytes\n";

    totalTableBytes += data.size();

    writeU32(
        out,
        static_cast<uint32_t>(data.size()));

    if (!data.empty()) {
        out.write(
            reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
    }
}

std::cerr
    << "Huffman tables total: "
    << totalTableBytes
    << " bytes\n";

 /* * Valid bit count of the complete stream. */ writeU64( out, writer.bitCount()); /* * Compressed stream. */ if (!compressed.empty()) { out.write( reinterpret_cast<const char*>( compressed.data()), static_cast<std::streamsize>( compressed.size())); } if (!out) return false; stats.originalSize = header.original_size; stats.finalTokenCount = tokens.size(); stats.tokenCount = tokenCount; stats.grammarCount = dictionary.size(); stats.compressedBits = writer.bitCount(); stats.compressedBytes = compressed.size(); stats.fileCount = static_cast<uint32_t>( index.size()); return true; }

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

/*
 * Optional grammar diagnostics.
 *
 * Enabled with:
 *
 *   GTC_GRAMMAR_STATS=1 ./gtc 20000.txt 20000.gtc
 *
 * We keep a copy of the grammar before
 * DEPS+LEFT reorder so we can compare:
 *
 *   original grammar
 *   reordered grammar
 */
    const bool grammarStats =
        std::getenv("GTC_GRAMMAR_STATS") != nullptr;

    GtcDictionary originalDictionary;

    if (grammarStats) {

        originalDictionary =
            dictionary;

        printGrammarCompactStats(
            "GRAMMAR BEFORE DEPS+LEFT REORDER",
            originalDictionary);

        printGrammarUsageStats(
            "GRAMMAR USAGE BEFORE REORDER",
            dictionary,
            sequence);

        printGrammarExpandedStats(
            "GRAMMAR EXPANDED SIZE BEFORE REORDER",
            dictionary,
            sequence);
    }

    if (!dictionary.reorderForEncoding(
            sequence)) {

        std::fprintf(
            stderr,
            "error: cannot reorder grammar\n");

        return false;
    }

    if (grammarStats) {

        printGrammarCompactStats(
            "GRAMMAR AFTER DEPS+LEFT REORDER",
            dictionary);

        printGrammarDistanceStats(
            "GRAMMAR BACK-DISTANCE AFTER REORDER",
            dictionary);

        printGrammarUsageStats(
            "GRAMMAR USAGE AFTER REORDER",
            dictionary,
            sequence);

        printGrammarExpandedStats(
            "GRAMMAR EXPANDED SIZE AFTER REORDER",
            dictionary,
            sequence);

        printGrammarValueStats(
            dictionary,
            sequence);
    }

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
    GtcArchiveStats& stats,
    bool perFileHuffman)
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

buildGrammarMultiFile(
    inputs,
    dictionary,
    sequence);

if (!dictionary.reorderForEncoding(
        sequence)) {

    std::fprintf(
        stderr,
        "error: cannot reorder grammar\n");

    return false;
}

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
        perFileHuffman,
        stats)) {

        std::fprintf(
            stderr,
            "error: cannot write %s\n",
            outputName.c_str());

        return false;
    }

    return true;
}