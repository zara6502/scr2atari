#include "gtc_dictionary.h"

GtcDictionary::GtcDictionary()
{
}

uint32_t GtcDictionary::size() const
{
    return static_cast<uint32_t>(rules_.size());
}

uint32_t GtcDictionary::addRule(uint32_t left, uint32_t right)
{
    rules_.push_back({left, right});

    // Token IDs start at 256.
    return 256u + static_cast<uint32_t>(rules_.size() - 1);
}

const GtcRule& GtcDictionary::rule(uint32_t token) const
{
    return rules_.at(token - 256u);
}

void GtcDictionary::clear()
{
    rules_.clear();
}