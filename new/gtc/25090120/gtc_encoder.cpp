#include "gtc_table.h"
#include "gtc_encoder.h"
#include "gtc_format.h"
#include "gtc_huffman.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>


namespace {


static constexpr uint32_t OPT_CANDIDATE_POOL = 64;
static constexpr uint32_t OPT_HUFFMAN_CANDIDATES = 24;

static constexpr uint32_t OPT_PAYLOAD_BEAM = 4;
static constexpr uint32_t OPT_TOTAL_BEAM = 4;

static constexpr uint32_t OPT_MIN_PAIR_COUNT = 2;

static constexpr int OPT_MAX_ROUNDS = 100;
static constexpr int OPT_SHANNON_PASSES = 3;

static constexpr double OPT_SHANNON_ALPHA = 0.25;


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
 * --------------------------------------------------------------------------
 * Practical grammar optimizer
 * --------------------------------------------------------------------------
 *
 * The grammar itself is still constructed by the original greedy pass.
 *
 * The optimizer then treats the current sequence as a token stream and
 * searches for useful additional pair rules.
 *
 * For one candidate rule:
 *
 *     A B -> N
 *
 * only the occurrences of A,B can change. Therefore the DP is equivalent
 * to a weighted interval scheduling problem over all occurrences of that
 * pair. This is considerably cheaper than running a complete token parser
 * for every candidate.
 *
 * Shannon code lengths are used only as a cheap ranking model.
 * Real Huffman is constructed only for the best candidates.
 */


struct OccurrenceList {
    uint32_t left = 0;
    uint32_t right = 0;

    std::vector<uint32_t> positions;
};


struct OptimizerCandidate {
    uint32_t left = 0;
    uint32_t right = 0;

    uint32_t count = 0;

    uint32_t replacementCount = 0;

    double shannonCost =
        std::numeric_limits<double>::infinity();

    std::vector<uint32_t> sequence;
};


struct OptimizerState {
    GtcDictionary dictionary;

    std::vector<uint32_t> sequence;

    uint64_t payloadBits = UINT64_MAX;
    uint64_t totalBytes = UINT64_MAX;

    uint64_t totalBits = UINT64_MAX;
};


struct ExactEncoding {
    uint64_t payloadBits = UINT64_MAX;
    uint64_t compressedBytes = UINT64_MAX;
    uint64_t totalBytes = UINT64_MAX;

    std::vector<uint8_t> tableData;
};


/*
 * Calculate frequencies of a token sequence.
 */
std::vector<uint64_t> frequenciesFromSequence(
    const std::vector<uint32_t>& sequence,
    uint32_t tokenCount)
{
    std::vector<uint64_t> frequencies(
        tokenCount,
        0);

    for (uint32_t token : sequence) {

        if (token >= tokenCount)
            continue;

        ++frequencies[token];
    }

    return frequencies;
}


/*
 * Shannon estimate.
 *
 * alpha keeps zero-frequency symbols finite so that the DP can still
 * consider introducing a new token.
 */
double shannonSymbolCost(
    uint64_t frequency,
    uint64_t total,
    uint32_t tokenCount)
{
    const double alpha =
        OPT_SHANNON_ALPHA;

    const double numerator =
        static_cast<double>(frequency) +
        alpha;

    const double denominator =
        static_cast<double>(total) +
        alpha *
        static_cast<double>(tokenCount);

    if (numerator <= 0.0 ||
        denominator <= 0.0)
        return 64.0;

    return
        -std::log2(
            numerator /
            denominator);
}


/*
 * Build Shannon costs for all currently possible tokens.
 */
std::vector<double> buildShannonCosts(
    const std::vector<uint64_t>& frequencies)
{
    const uint32_t tokenCount =
        static_cast<uint32_t>(
            frequencies.size());

    uint64_t total = 0;

    for (uint64_t frequency :
         frequencies) {

        if (UINT64_MAX - total < frequency)
            total = UINT64_MAX;
        else
            total += frequency;
    }

    std::vector<double> costs(
        tokenCount,
        64.0);

    if (total == 0)
        return costs;

    for (uint32_t token = 0;
         token < tokenCount;
         ++token) {

        costs[token] =
            shannonSymbolCost(
                frequencies[token],
                total,
                tokenCount);
    }

    return costs;
}


/*
 * Count non-overlapping replacements available for one pair.
 *
 * The original greedy grammar uses the same left-to-right replacement
 * semantics.
 */
uint32_t countNonOverlapping(
    const std::vector<uint32_t>& positions)
{
    uint32_t count = 0;

    uint32_t previousEnd = 0;

    bool havePrevious = false;

    for (uint32_t position :
         positions) {

        if (!havePrevious ||
            position >= previousEnd) {

            ++count;

            previousEnd =
                position + 2;

            havePrevious = true;
        }
    }

    return count;
}


/*
 * Parse one candidate using weighted interval scheduling.
 *
 * Every occurrence is an interval [position, position+2).
 *
 * benefit =
 *
 *     cost(A) + cost(B) - cost(N)
 *
 * Positive benefit means that replacing the pair reduces the estimated
 * Shannon payload.
 */
OptimizerCandidate parseCandidate(
    const std::vector<uint32_t>& parentSequence,
    const OccurrenceList& occurrence,
    uint32_t newToken,
    const std::vector<double>& costs)
{
    OptimizerCandidate result;

    result.left =
        occurrence.left;

    result.right =
        occurrence.right;

    result.count =
        static_cast<uint32_t>(
            occurrence.positions.size());

    result.replacementCount =
        countNonOverlapping(
            occurrence.positions);

    if (result.replacementCount == 0) {

        result.sequence =
            parentSequence;

        result.shannonCost =
            0.0;

        return result;
    }

    const double leftCost =
        occurrence.left < costs.size()
            ? costs[occurrence.left]
            : 64.0;

    const double rightCost =
        occurrence.right < costs.size()
            ? costs[occurrence.right]
            : 64.0;

    const double newCost =
        newToken < costs.size()
            ? costs[newToken]
            : 64.0;

    const double benefit =
        leftCost +
        rightCost -
        newCost;

    const size_t count =
        occurrence.positions.size();

    /*
     * value[i] = best saving where occurrence i is the last selected
     * interval.
     */
    std::vector<double> value(
        count,
        0.0);

    std::vector<int32_t> previous(
        count,
        -1);

    double prefixBest = 0.0;

    int32_t prefixIndex = -1;

    size_t eligible = 0;

    double bestSaving = 0.0;

    int32_t bestIndex = -1;

    for (size_t i = 0;
         i < count;
         ++i) {

        const uint32_t position =
            occurrence.positions[i];

        /*
         * Any interval beginning at <= position-2 can precede this one.
         */
        while (eligible < i) {

            const uint32_t previousPosition =
                occurrence.positions[eligible];

            if (previousPosition + 2 >
                position)
                break;

            if (value[eligible] >
                prefixBest) {

                prefixBest =
                    value[eligible];

                prefixIndex =
                    static_cast<int32_t>(
                        eligible);
            }

            ++eligible;
        }

        if (benefit <= 0.0)
            continue;

        const double current =
            prefixBest +
            benefit;

        value[i] =
            current;

        previous[i] =
            prefixIndex;

        if (current >
            bestSaving) {

            bestSaving =
                current;

            bestIndex =
                static_cast<int32_t>(
                    i);
        }
    }

    /*
     * No profitable replacement.
     */
    if (bestIndex < 0) {

        result.sequence =
            parentSequence;

        result.shannonCost =
            0.0;

        return result;
    }

    /*
     * Recover selected intervals.
     */
    std::vector<uint8_t> selected(
        count,
        0);

    int32_t current =
        bestIndex;

    while (current >= 0) {

        selected[
            static_cast<size_t>(
                current)] = 1;

        current =
            previous[
                static_cast<size_t>(
                    current)];
    }

    /*
     * Construct the actual token sequence.
     */
    result.sequence.clear();

    result.sequence.reserve(
        parentSequence.size());

    size_t occurrenceIndex = 0;

    size_t i = 0;

    while (i < parentSequence.size()) {

        if (i + 1 < parentSequence.size() &&
            occurrenceIndex < count &&
            occurrence.positions[
                occurrenceIndex] == i) {

            if (selected[occurrenceIndex]) {

                result.sequence.push_back(
                    newToken);

                i += 2;

                ++occurrenceIndex;

                continue;
            }

            ++occurrenceIndex;
        }

        result.sequence.push_back(
            parentSequence[i]);

        ++i;
    }

    /*
     * Exact estimated Shannon payload for the resulting sequence.
     */
    double cost = 0.0;

    for (uint32_t token :
         result.sequence) {

        if (token < costs.size())
            cost += costs[token];
    }

    result.shannonCost =
        cost;

    result.replacementCount =
        static_cast<uint32_t>(
            countNonOverlapping(
                occurrence.positions));

    return result;
}


/*
 * Build the list of currently existing grammar pairs.
 */
std::unordered_set<uint64_t> buildExistingPairs(
    const GtcDictionary& dictionary)
{
    std::unordered_set<uint64_t> result;

    result.reserve(
        dictionary.size() * 2u + 16u);

    for (uint32_t i = 0;
         i < dictionary.size();
         ++i) {

        const GtcRule& rule =
            dictionary.rule(
                GTC_BASE_TOKENS + i);

        result.insert(
            pairKey(
                rule.left,
                rule.right));
    }

    return result;
}


/*
 * Find the most frequent candidate pairs.
 */
std::vector<PairInfo> findCandidatePairs(
    const std::vector<uint32_t>& sequence,
    const GtcDictionary& dictionary)
{
    std::unordered_map<
        uint64_t,
        PairInfo> pairs;

    pairs.reserve(
        sequence.size());

    const auto existing =
        buildExistingPairs(
            dictionary);

    for (size_t i = 0;
         i + 1 < sequence.size();
         ++i) {

        const uint32_t left =
            sequence[i];

        const uint32_t right =
            sequence[i + 1];

        const uint64_t key =
            pairKey(
                left,
                right);

        if (existing.find(key) !=
            existing.end())
            continue;

        auto it =
            pairs.find(key);

        if (it == pairs.end()) {

            PairInfo info;

            info.left =
                left;

            info.right =
                right;

            info.count =
                1;

            pairs.emplace(
                key,
                info);
        }
        else {
            ++it->second.count;
        }
    }

    std::vector<PairInfo> result;

    result.reserve(
        pairs.size());

    for (const auto& item :
         pairs) {

        if (item.second.count >=
            OPT_MIN_PAIR_COUNT) {

            result.push_back(
                item.second);
        }
    }

    std::sort(
        result.begin(),
        result.end(),
        [](const PairInfo& a,
           const PairInfo& b) {

            if (a.count != b.count)
                return a.count > b.count;

            return
                pairKey(
                    a.left,
                    a.right) <
                pairKey(
                    b.left,
                    b.right);
        });

    if (result.size() >
        OPT_CANDIDATE_POOL) {

        result.resize(
            OPT_CANDIDATE_POOL);
    }

    return result;
}


/*
 * Build occurrence lists for only the selected candidate pairs.
 *
 * This is considerably cheaper than storing occurrence positions for every
 * pair in the sequence.
 */
std::vector<OccurrenceList> buildOccurrenceLists(
    const std::vector<uint32_t>& sequence,
    const std::vector<PairInfo>& pairs)
{
    std::vector<OccurrenceList> result(
        pairs.size());

    std::unordered_map<
        uint64_t,
        size_t> index;

    index.reserve(
        pairs.size() * 2u + 1u);

    for (size_t i = 0;
         i < pairs.size();
         ++i) {

        result[i].left =
            pairs[i].left;

        result[i].right =
            pairs[i].right;

        index.emplace(
            pairKey(
                pairs[i].left,
                pairs[i].right),
            i);
    }

    for (size_t i = 0;
         i + 1 < sequence.size();
         ++i) {

        const uint64_t key =
            pairKey(
                sequence[i],
                sequence[i + 1]);

        auto it =
            index.find(key);

        if (it == index.end())
            continue;

        result[
            it->second].positions.push_back(
                static_cast<uint32_t>(i));
    }

    return result;
}


/*
 * Cheap candidate evaluation.
 *
 * No Huffman tree is constructed here.
 */
std::vector<OptimizerCandidate> evaluateCandidates(
    const OptimizerState& parent)
{
    const std::vector<PairInfo> pairs =
        findCandidatePairs(
            parent.sequence,
            parent.dictionary);

    std::vector<OptimizerCandidate> result;

    if (pairs.empty())
        return result;

    const std::vector<OccurrenceList>
        occurrences =
            buildOccurrenceLists(
                parent.sequence,
                pairs);

    const uint32_t newToken =
        GTC_BASE_TOKENS +
        parent.dictionary.size();

    if (newToken > UINT16_MAX)
        return result;

    const uint32_t tokenCount =
        newToken + 1;

    std::vector<uint64_t> frequencies =
        frequenciesFromSequence(
            parent.sequence,
            tokenCount);

    result.reserve(
        pairs.size());

    for (size_t candidateIndex = 0;
         candidateIndex < pairs.size();
         ++candidateIndex) {

        const OccurrenceList& occurrence =
            occurrences[candidateIndex];

        if (occurrence.positions.empty())
            continue;

        const uint32_t replacementCount =
            countNonOverlapping(
                occurrence.positions);

        if (replacementCount <
            OPT_MIN_PAIR_COUNT)
            continue;

        /*
         * Three Shannon/DP passes.
         *
         * The first estimate gives the new token the maximum useful
         * frequency. Subsequent passes use the actual frequency of the
         * resulting parsed sequence.
         */
        std::vector<uint64_t>
            currentFrequencies =
                frequencies;

        currentFrequencies[newToken] =
            replacementCount;

        OptimizerCandidate best;

        for (int pass = 0;
             pass < OPT_SHANNON_PASSES;
             ++pass) {

            const std::vector<double>
                costs =
                    buildShannonCosts(
                        currentFrequencies);

            OptimizerCandidate parsed =
                parseCandidate(
                    parent.sequence,
                    occurrence,
                    newToken,
                    costs);

            if (parsed.sequence.empty() &&
                !parent.sequence.empty())
                continue;

            currentFrequencies =
                frequenciesFromSequence(
                    parsed.sequence,
                    tokenCount);

            best =
                std::move(parsed);
        }

        if (best.sequence.empty() &&
            !parent.sequence.empty())
            continue;

        if (best.replacementCount <
            OPT_MIN_PAIR_COUNT)
            continue;

        result.push_back(
            std::move(best));
    }

    std::sort(
        result.begin(),
        result.end(),
        [](const OptimizerCandidate& a,
           const OptimizerCandidate& b) {

            if (a.shannonCost !=
                b.shannonCost)
                return
                    a.shannonCost <
                    b.shannonCost;

            if (a.replacementCount !=
                b.replacementCount)
                return
                    a.replacementCount >
                    b.replacementCount;

            if (a.left != b.left)
                return a.left < b.left;

            return a.right < b.right;
        });

    return result;
}


/*
 * Exact size of a single-file GTC stream.
 *
 * Current 25090100 format:
 *
 *     header             40 bytes
 *     grammar            4 * grammarCount
 *     table size         4
 *     table data
 *     bit count          8
 *     compressed bytes
 */
uint64_t exactSingleFileSize(
    uint32_t grammarCount,
    const std::vector<uint8_t>& tableData,
    uint64_t compressedBytes)
{
    uint64_t result = 40;

    result +=
        static_cast<uint64_t>(
            grammarCount) * 4ULL;

    result += 4ULL;

    result +=
        static_cast<uint64_t>(
            tableData.size());

    result += 8ULL;

    result +=
        compressedBytes;

    return result;
}


/*
 * Build the real Huffman representation and calculate the exact archive
 * size for one optimizer state.
 */
bool evaluateExactState(
    const OptimizerState& state,
    ExactEncoding& result)
{
    const uint32_t tokenCount =
        GTC_BASE_TOKENS +
        state.dictionary.size();

    std::vector<uint64_t> frequencies =
        frequenciesFromSequence(
            state.sequence,
            tokenCount);

    GtcHuffman huffman;

    if (!huffman.build(
            frequencies))
        return false;

    BitWriter writer;

    huffman.encode(
        state.sequence,
        writer);

    writer.flush();

    GtcLengthTable table;

    GtcTableStats tableStats;

    std::vector<uint8_t> tableData;

    if (!table.encode(
            huffman.codeLengths(),
            tableData,
            tableStats))
        return false;

    result.payloadBits =
        writer.bitCount();

    result.compressedBytes =
        writer.data().size();

    result.tableData =
        std::move(tableData);

    result.totalBytes =
        exactSingleFileSize(
            state.dictionary.size(),
            result.tableData,
            result.compressedBytes);

    return true;
}


/*
 * Create a real state from a cheap candidate.
 */
bool makeExactChild(
    const OptimizerState& parent,
    const OptimizerCandidate& candidate,
    OptimizerState& child)
{
    child.dictionary =
        parent.dictionary;

    const uint32_t newToken =
        child.dictionary.addRule(
            candidate.left,
            candidate.right);

    if (newToken >
        UINT16_MAX)
        return false;

    child.sequence =
        candidate.sequence;

    ExactEncoding exact;

    if (!evaluateExactState(
            child,
            exact))
        return false;

    child.payloadBits =
        exact.payloadBits;

    child.totalBytes =
        exact.totalBytes;

    child.totalBits =
        child.totalBytes * 8ULL;

    return true;
}


/*
 * Compare two states by exact complete file size.
 */
bool betterTotal(
    const OptimizerState& a,
    const OptimizerState& b)
{
    if (a.totalBytes !=
        b.totalBytes)
        return
            a.totalBytes <
            b.totalBytes;

    if (a.payloadBits !=
        b.payloadBits)
        return
            a.payloadBits <
            b.payloadBits;

    if (a.dictionary.size() !=
        b.dictionary.size())
        return
            a.dictionary.size() <
            b.dictionary.size();

    return
        a.sequence.size() <
        b.sequence.size();
}


/*
 * Compare by payload only.
 *
 * This is deliberately separate from the final exact-size criterion.
 */
bool betterPayload(
    const OptimizerState& a,
    const OptimizerState& b)
{
    if (a.payloadBits !=
        b.payloadBits)
        return
            a.payloadBits <
            b.payloadBits;

    if (a.totalBytes !=
        b.totalBytes)
        return
            a.totalBytes <
            b.totalBytes;

    return
        a.dictionary.size() <
        b.dictionary.size();
}


/*
 * Same grammar means that the state is equivalent for beam purposes.
 */
bool sameDictionary(
    const GtcDictionary& a,
    const GtcDictionary& b)
{
    if (a.size() !=
        b.size())
        return false;

    for (uint32_t i = 0;
         i < a.size();
         ++i) {

        const GtcRule& ra =
            a.rule(
                GTC_BASE_TOKENS + i);

        const GtcRule& rb =
            b.rule(
                GTC_BASE_TOKENS + i);

        if (ra.left != rb.left ||
            ra.right != rb.right)
            return false;
    }

    return true;
}


/*
 * Remove duplicate grammar states from a beam.
 */
void deduplicateStates(
    std::vector<OptimizerState>& states)
{
    std::vector<OptimizerState> result;

    result.reserve(
        states.size());

    for (OptimizerState& state :
         states) {

        bool duplicate = false;

        for (const OptimizerState& existing :
             result) {

            if (sameDictionary(
                    state.dictionary,
                    existing.dictionary)) {

                duplicate = true;
                break;
            }
        }

        if (!duplicate)
            result.push_back(
                std::move(state));
    }

    states.swap(result);
}


/*
 * Generate exact children of one beam state.
 *
 * Only the best 24 Shannon candidates receive an actual Huffman model.
 */
std::vector<OptimizerState> generateChildren(
    const OptimizerState& parent)
{
    std::vector<OptimizerState> result;

    /*
     * Keeping the parent is important:
     * an optimization round is allowed to make no change.
     */
    result.push_back(
        parent);

    std::vector<OptimizerCandidate>
        candidates =
            evaluateCandidates(
                parent);

    if (candidates.empty())
        return result;

    if (candidates.size() >
        OPT_HUFFMAN_CANDIDATES) {

        candidates.resize(
            OPT_HUFFMAN_CANDIDATES);
    }

    std::vector<OptimizerState>
        exactCandidates;

    exactCandidates.reserve(
        candidates.size());

    for (const OptimizerCandidate& candidate :
         candidates) {

        OptimizerState child;

        if (!makeExactChild(
                parent,
                candidate,
                child))
            continue;

        exactCandidates.push_back(
            std::move(child));
    }

    /*
     * We deliberately construct exact Huffman models only for the
     * Shannon-ranked candidates above.
     *
     * The dual beam is selected from these exact candidates.
     */
    for (OptimizerState& child :
         exactCandidates) {

        result.push_back(
            std::move(child));
    }

    return result;
}


/*
 * Select:
 *
 *     top 4 by total size
 *     top 4 by payload
 *
 * and merge them into the next beam.
 */
std::vector<OptimizerState> selectBeam(
    std::vector<OptimizerState> states)
{
    deduplicateStates(
        states);

    std::vector<OptimizerState> next;

    if (states.empty())
        return next;

    /*
     * Total-size beam.
     */
    std::vector<size_t> totalOrder(
        states.size());

    for (size_t i = 0;
         i < states.size();
         ++i)
        totalOrder[i] = i;

    std::sort(
        totalOrder.begin(),
        totalOrder.end(),
        [&states](size_t a,
                  size_t b) {

            return betterTotal(
                states[a],
                states[b]);
        });

    const size_t totalCount =
        std::min(
            static_cast<size_t>(
                OPT_TOTAL_BEAM),
            totalOrder.size());

    for (size_t i = 0;
         i < totalCount;
         ++i) {

        next.push_back(
            std::move(
                states[
                    totalOrder[i]]));
    }

    /*
     * Payload beam.
     */
    std::sort(
        states.begin(),
        states.end(),
        [](const OptimizerState& a,
           const OptimizerState& b) {

            return betterPayload(
                a,
                b);
        });

    for (size_t i = 0;
         i < states.size() &&
         i < OPT_PAYLOAD_BEAM;
         ++i) {

        bool duplicate = false;

        for (const OptimizerState& existing :
             next) {

            if (sameDictionary(
                    existing.dictionary,
                    states[i].dictionary)) {

                duplicate = true;
                break;
            }
        }

        if (!duplicate) {

            next.push_back(
                std::move(states[i]));
        }
    }

    deduplicateStates(
        next);

    /*
     * Safety cap. Normally the dual beam gives at most 8 states.
     */
    std::sort(
        next.begin(),
        next.end(),
        [](const OptimizerState& a,
           const OptimizerState& b) {

            return betterTotal(
                a,
                b);
        });

    const size_t maxBeam =
        OPT_PAYLOAD_BEAM +
        OPT_TOTAL_BEAM;

    if (next.size() >
        maxBeam) {

        next.resize(
            maxBeam);
    }

    return next;
}


/*
 * Practical grammar optimizer.
 *
 * The original greedy grammar is the starting state.
 */
void optimizeGrammar(
    GtcDictionary& dictionary,
    std::vector<uint32_t>& sequence)
{
    OptimizerState initial;

    initial.dictionary =
        dictionary;

    initial.sequence =
        sequence;

    ExactEncoding initialExact;

    if (!evaluateExactState(
            initial,
            initialExact))
        return;

    initial.payloadBits =
        initialExact.payloadBits;

    initial.totalBytes =
        initialExact.totalBytes;

    initial.totalBits =
        initial.totalBytes * 8ULL;

    OptimizerState best =
        initial;

    std::vector<OptimizerState> beam;

    beam.push_back(
        std::move(initial));

    for (int round = 0;
         round < OPT_MAX_ROUNDS;
         ++round) {

        std::vector<OptimizerState>
            children;

        /*
         * Each beam state produces its own candidate set.
         */
        for (const OptimizerState& parent :
             beam) {

            std::vector<OptimizerState>
                generated =
                    generateChildren(
                        parent);

            for (OptimizerState& state :
                 generated) {

                children.push_back(
                    std::move(state));
            }
        }

        if (children.empty())
            break;

        /*
         * Check global best before beam pruning.
         */
        for (const OptimizerState& state :
             children) {

            if (betterTotal(
                    state,
                    best)) {

                best =
                    state;
            }
        }

        std::vector<OptimizerState>
            next =
                selectBeam(
                    std::move(children));

        if (next.empty())
            break;

        /*
         * If the best state did not improve and every state is the
         * same as before, there is no useful work left.
         */
        bool hasNewGrammar = false;

        for (const OptimizerState& state :
             next) {

            if (!sameDictionary(
                    state.dictionary,
                    beam.front().dictionary)) {

                hasNewGrammar = true;
                break;
            }
        }

        beam =
            std::move(next);

        if (!hasNewGrammar)
            break;
    }

    dictionary =
        best.dictionary;

    sequence =
        best.sequence;
}


/*
 * --------------------------------------------------------------------------
 * Archive helpers
 * --------------------------------------------------------------------------
 */


/*
 * Write the common archive prefix:
 *
 *     header
 *     index
 *     names
 *     grammar
 *
 * The Huffman-specific part is written by the selected model below.
 */
bool writeArchiveCommon(
    std::ofstream& out,
    const std::vector<GtcArchiveFile>& index,
    const std::vector<uint8_t>& names,
    const GtcDictionary& dictionary)
{
    uint32_t nameOffset = 0;

    for (const GtcArchiveFile& entry :
         index) {

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

                info.left =
                    a;

                info.right =
                    b;

                info.count =
                    1;

                pairs.emplace(
                    key,
                    info);
            }
            else {
                ++it->second.count;
            }
        }

        PairInfo best;

        for (const auto& item :
             pairs) {

            const PairInfo& info =
                item.second;

            if (info.count >
                best.count)
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

    for (const auto& file :
         files)
        totalSize +=
            file.size();

    sequence.reserve(
        totalSize +
        files.size());

    /*
     * The special boundary token is only an internal
     * marker. It prevents grammar rules from crossing
     * from one file into another.
     */
    for (size_t fileIndex = 0;
         fileIndex < files.size();
         ++fileIndex) {

        for (uint8_t c :
             files[fileIndex])
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

                info.left =
                    a;

                info.right =
                    b;

                info.count =
                    1;

                pairs.emplace(
                    key,
                    info);
            }
            else {
                ++it->second.count;
            }
        }

        PairInfo best;

        for (const auto& item :
             pairs) {

            const PairInfo& info =
                item.second;

            if (info.count >
                best.count)
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

    for (uint32_t token :
         sequence) {

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

    for (uint32_t token :
         sequence) {

        if (token ==
            GTC_FILE_BOUNDARY)
            continue;

        if (token >= tokenCount)
            return false;

        tokens.push_back(token);
    }

    /*
     * Build filename table.
     */
    std::vector<uint8_t> names;

    for (const GtcArchiveFile& entry :
         files) {

        names.insert(
            names.end(),
            entry.name.begin(),
            entry.name.end());

        names.push_back(0);
    }

    const uint32_t indexEntrySize =
        40;

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

    std::vector<uint64_t>
        globalFrequencies(
            tokenCount,
            0);

    for (uint32_t token :
         tokens)
        ++globalFrequencies[token];

    if (!globalHuffman.build(
            globalFrequencies))
        return false;

    BitWriter globalWriter;

    std::vector<GtcArchiveFile>
        globalIndex =
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
            tokens.size() -
            tokenPosition)
            return false;

        const size_t count =
            static_cast<size_t>(
                entry.tokenCount);

        std::vector<uint32_t>
            fileTokens;

        fileTokens.reserve(
            count);

        for (size_t i = 0;
             i < count;
             ++i) {

            fileTokens.push_back(
                tokens[
                    tokenPosition + i]);
        }

        globalHuffman.encode(
            fileTokens,
            globalWriter);

        tokenPosition +=
            count;
    }

    if (tokenPosition !=
        tokens.size())
        return false;

    globalWriter.flush();

    GtcLengthTable lengthTable;

    std::vector<uint8_t>
        globalTableData;

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

    if (globalFixed ==
        UINT64_MAX)
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
     */
    bool deltaCandidateValid =
        true;

    std::vector<GtcHuffman>
        fileHuffmans;

    BitWriter deltaMainWriter;

    std::vector<GtcArchiveFile>
        deltaIndex =
            files;

    /*
     * An empty file cannot have an independent Huffman
     * model. In that case simply disable the delta candidate.
     */
    for (const GtcArchiveFile& entry :
         files) {

        if (entry.tokenCount == 0) {

            deltaCandidateValid =
                false;

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

            std::vector<uint64_t>
                frequencies(
                    tokenCount,
                    0);

            if (entry.tokenCount >
                tokens.size() -
                tokenPosition) {

                deltaCandidateValid =
                    false;

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

                deltaCandidateValid =
                    false;

                break;
            }

            tokenPosition +=
                static_cast<size_t>(
                    entry.tokenCount);
        }

        if (tokenPosition !=
            tokens.size())
            deltaCandidateValid =
                false;
    }

    std::vector<int8_t>
        deltaValues;

    std::vector<uint8_t>
        deltaSymbols;

    GtcHuffman deltaHuffman;

    std::vector<uint8_t>
        deltaTableData;

    BitWriter deltaWriter;

    if (deltaCandidateValid) {

        std::map<int, uint32_t>
            deltaMap;

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

                deltaCandidateValid =
                    false;

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

                    deltaCandidateValid =
                        false;

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
            deltaCandidateValid =
                false;

        if (deltaValues.size() > 256)
            deltaCandidateValid =
                false;

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

                    if (it ==
                        deltaMap.end()) {

                        deltaCandidateValid =
                            false;

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

                deltaCandidateValid =
                    false;

                break;
            }

            ++deltaFrequencies[symbol];
        }

        if (deltaCandidateValid) {

            if (!deltaHuffman.build(
                    deltaFrequencies)) {

                deltaCandidateValid =
                    false;
            }
        }
    }

    if (deltaCandidateValid) {

        std::vector<uint32_t>
            deltaHuffmanSymbols;

        deltaHuffmanSymbols.reserve(
            deltaSymbols.size());

        for (uint8_t symbol :
             deltaSymbols) {

            deltaHuffmanSymbols.push_back(
                static_cast<uint32_t>(
                    symbol));
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

            deltaCandidateValid =
                false;
        }
    }

    if (deltaCandidateValid) {

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
                tokens.size() -
                tokenPosition) {

                deltaCandidateValid =
                    false;

                break;
            }

            const size_t count =
                static_cast<size_t>(
                    entry.tokenCount);

            std::vector<uint32_t>
                fileTokens;

            fileTokens.reserve(
                count);

            for (size_t i = 0;
                 i < count;
                 ++i) {

                fileTokens.push_back(
                    tokens[
                        tokenPosition + i]);
            }

            fileHuffmans[fileIndex].encode(
                fileTokens,
                deltaMainWriter);

            tokenPosition +=
                count;
        }

        if (tokenPosition !=
            tokens.size())
            deltaCandidateValid =
                false;
    }

    if (deltaCandidateValid)
        deltaMainWriter.flush();

    uint64_t deltaArchiveSize =
        UINT64_MAX;

    if (deltaCandidateValid) {

        const uint64_t deltaFixed =
            archiveFixedSize(
                60,
                indexSize64,
                names.size(),
                dictionary.size());

        if (deltaFixed !=
            UINT64_MAX) {

            deltaArchiveSize =
                deltaFixed;

            if (!addArchiveSize(
                    deltaArchiveSize,
                    4ULL +
                        globalTableData.size(),
                    deltaArchiveSize))
                deltaArchiveSize =
                    UINT64_MAX;

            if (deltaArchiveSize !=
                UINT64_MAX) {

                if (!addArchiveSize(
                        deltaArchiveSize,
                        2ULL +
                            deltaValues.size(),
                        deltaArchiveSize))
                    deltaArchiveSize =
                        UINT64_MAX;
            }

            if (deltaArchiveSize !=
                UINT64_MAX) {

                if (!addArchiveSize(
                        deltaArchiveSize,
                        4ULL +
                            deltaTableData.size(),
                        deltaArchiveSize))
                    deltaArchiveSize =
                        UINT64_MAX;
            }

            if (deltaArchiveSize !=
                UINT64_MAX) {

                if (!addArchiveSize(
                        deltaArchiveSize,
                        deltaWriter.data().size(),
                        deltaArchiveSize))
                    deltaArchiveSize =
                        UINT64_MAX;
            }

            if (deltaArchiveSize !=
                UINT64_MAX) {

                if (!addArchiveSize(
                        deltaArchiveSize,
                        8ULL,
                        deltaArchiveSize))
                    deltaArchiveSize =
                        UINT64_MAX;
            }

            if (deltaArchiveSize !=
                UINT64_MAX) {

                if (!addArchiveSize(
                        deltaArchiveSize,
                        deltaMainWriter.data().size(),
                        deltaArchiveSize))
                    deltaArchiveSize =
                        UINT64_MAX;
            }
        }
    }

    /*
     * Automatic selection.
     *
     * Equal size -> global v6.
     */
    const bool useDelta =
        deltaCandidateValid &&
        deltaArchiveSize <
            globalArchiveSize;

    std::ofstream out(
        filename,
        std::ios::binary);

    if (!out)
        return false;

    uint64_t archiveOriginalSize =
        0;

    for (const GtcArchiveFile& entry :
         files) {

        if (!addArchiveSize(
                archiveOriginalSize,
                entry.originalSize,
                archiveOriginalSize))
            return false;
    }

    if (!useDelta) {

        /*
         * ----------------------------------------------------
         * Write v6 global archive.
         * ----------------------------------------------------
         */
        const std::vector<GtcArchiveFile>&
            index =
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
        const std::vector<GtcArchiveFile>&
            index =
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

    /*
     * Step 1:
     *
     * Original greedy grammar construction.
     */
    buildGrammar(
        input,
        dictionary,
        sequence);

    /*
     * Step 2:
     *
     * Practical optimizer.
     *
     * This changes only the grammar/sequence chosen for the single-file
     * encoder. The GTC format and decoder remain untouched.
     */
    optimizeGrammar(
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