#include <cassert>
#include "gtc_dictionary.h"

GtcDictionary::GtcDictionary()
{
}

uint32_t GtcDictionary::size() const
{
    return static_cast<uint32_t>(rules_.size());
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
    // Token IDs start at 256.
    return 256u +
        static_cast<uint32_t>(
            rules_.size() - 1);
}

const GtcRule& GtcDictionary::rule(
    uint32_t token) const
{
    return rules_.at(token - 256u);
}

void GtcDictionary::clear()
{
    rules_.clear();
}