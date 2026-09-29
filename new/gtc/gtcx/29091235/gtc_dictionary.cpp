#include <cassert>
#include <cstdint>
#include <vector>

#include "gtc_dictionary.h"

namespace {

static constexpr uint32_t BASE_TOKEN = 256;

static uint32_t zigzag(int32_t value)
{
    return
        (static_cast<uint32_t>(value) << 1) ^
        static_cast<uint32_t>(value >> 31);
}

static uint32_t varintSize(uint32_t value)
{
    uint32_t n = 1;

    while (value >= 128) {
        value >>= 7;
        ++n;
    }

    return n;
}

static uint32_t deltaCost(
    uint32_t previous,
    uint32_t value)
{
    const int32_t delta =
        static_cast<int32_t>(value) -
        static_cast<int32_t>(previous);

    return varintSize(
        zigzag(delta));
}

} // namespace

GtcDictionary::GtcDictionary()
{
}

uint32_t GtcDictionary::size() const
{
    return static_cast<uint32_t>(
        rules_.size());
}

uint32_t GtcDictionary::addRule(
    uint32_t left,
    uint32_t right)
{
    assert(left <= UINT16_MAX);
    assert(right <= UINT16_MAX);

    rules_.push_back({
        static_cast<uint16_t>(left),
        static_cast<uint16_t>(right)
    });

    return BASE_TOKEN +
        static_cast<uint32_t>(
            rules_.size() - 1);
}

const GtcRule& GtcDictionary::rule(
    uint32_t token) const
{
    return rules_.at(
        token - BASE_TOKEN);
}

void GtcDictionary::clear()
{
    rules_.clear();
}

bool GtcDictionary::reorderForEncoding(
    std::vector<uint32_t>& sequence)
{
    const uint32_t count =
        static_cast<uint32_t>(
            rules_.size());

    if (count < 2)
        return true;

    /*
     * Number of direct dependents.
     *
     * If rule R is referenced by another rule,
     * R receives one dependency point.
     */
    std::vector<uint32_t> dependents(
        count,
        0);

    for (uint32_t i = 0;
         i < count;
         ++i) {

        const GtcRule& r =
            rules_[i];

        if (r.left >= BASE_TOKEN) {

            const uint32_t dependency =
                static_cast<uint32_t>(r.left) -
                BASE_TOKEN;

            if (dependency >= count)
                return false;

            ++dependents[dependency];
        }

        if (r.right >= BASE_TOKEN) {

            const uint32_t dependency =
                static_cast<uint32_t>(r.right) -
                BASE_TOKEN;

            if (dependency >= count)
                return false;

            ++dependents[dependency];
        }
    }

    std::vector<uint8_t> emitted(
        count,
        0);

    std::vector<uint32_t> order;
    order.reserve(count);

    uint32_t previousLeft = 0;
    uint32_t previousRight = 0;

    /*
     * DEPS+LEFT
     *
     * Primary:
     *     delta cost
     *
     * Tie-break:
     *     number of dependents, descending
     *     left, ascending
     *     right, ascending
     *     original rule index, ascending
     *
     * This must match the standalone grammar-order
     * test exactly.
     */
    for (uint32_t step = 0;
         step < count;
         ++step) {

        bool haveBest = false;

        uint32_t bestRule = 0;
        uint32_t bestCost = 0;
        uint32_t bestDeps = 0;
        uint16_t bestLeft = 0;
        uint16_t bestRight = 0;

        for (uint32_t i = 0;
             i < count;
             ++i) {

            if (emitted[i])
                continue;

            const GtcRule& r =
                rules_[i];

            /*
             * Rule is available only when all
             * grammar dependencies have already
             * been emitted.
             */
            bool available = true;

            if (r.left >= BASE_TOKEN) {

                const uint32_t dependency =
                    static_cast<uint32_t>(r.left) -
                    BASE_TOKEN;

                if (!emitted[dependency])
                    available = false;
            }

            if (r.right >= BASE_TOKEN) {

                const uint32_t dependency =
                    static_cast<uint32_t>(r.right) -
                    BASE_TOKEN;

                if (!emitted[dependency])
                    available = false;
            }

            if (!available)
                continue;

            const uint32_t cost =
                deltaCost(
                    previousLeft,
                    r.left) +
                deltaCost(
                    previousRight,
                    r.right);

            const uint32_t deps =
                dependents[i];

            bool better = false;

            if (!haveBest) {
                better = true;
            }
            else if (cost < bestCost) {
                better = true;
            }
            else if (cost == bestCost &&
                     deps > bestDeps) {
                better = true;
            }
            else if (cost == bestCost &&
                     deps == bestDeps &&
                     r.left < bestLeft) {
                better = true;
            }
            else if (cost == bestCost &&
                     deps == bestDeps &&
                     r.left == bestLeft &&
                     r.right < bestRight) {
                better = true;
            }
            else if (cost == bestCost &&
                     deps == bestDeps &&
                     r.left == bestLeft &&
                     r.right == bestRight &&
                     i < bestRule) {
                better = true;
            }

            if (better) {

                haveBest = true;

                bestRule = i;
                bestCost = cost;
                bestDeps = deps;
                bestLeft = r.left;
                bestRight = r.right;
            }
        }

        if (!haveBest)
            return false;

        emitted[bestRule] = 1;

        order.push_back(
            bestRule);

        previousLeft =
            rules_[bestRule].left;

        previousRight =
            rules_[bestRule].right;
    }

    /*
     * Old grammar token -> new grammar token.
     *
     * Literal bytes remain unchanged.
     */
    std::vector<uint32_t> remap(
        BASE_TOKEN + count);

    for (uint32_t i = 0;
         i < BASE_TOKEN;
         ++i) {

        remap[i] = i;
    }

    for (uint32_t newIndex = 0;
         newIndex < count;
         ++newIndex) {

        const uint32_t oldIndex =
            order[newIndex];

        remap[
            BASE_TOKEN + oldIndex] =
            BASE_TOKEN + newIndex;
    }

    /*
     * Rebuild grammar in the new topological order.
     *
     * All grammar references must also be remapped.
     */
    const std::vector<GtcRule> oldRules =
        rules_;

    std::vector<GtcRule> newRules(
        count);

    for (uint32_t newIndex = 0;
         newIndex < count;
         ++newIndex) {

        const GtcRule& oldRule =
            oldRules[
                order[newIndex]];

        newRules[newIndex].left =
            static_cast<uint16_t>(
                remap[oldRule.left]);

        newRules[newIndex].right =
            static_cast<uint16_t>(
                remap[oldRule.right]);
    }

    rules_.swap(newRules);

    /*
     * Remap final token stream.
     *
     * 0..255 = literals
     * 256+   = grammar tokens
     * FFFFFFFF = archive boundary
     */
    for (uint32_t& token : sequence) {

        if (token >= BASE_TOKEN &&
            token < BASE_TOKEN + count) {

            token = remap[token];
        }
    }

    return true;
}