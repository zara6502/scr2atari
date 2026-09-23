#include "gtc_encoder.h"
#include "gtc_format.h"
#include "gtc_huffman.h"

#include <cstdio>
#include <fstream>
#include <iostream>
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
