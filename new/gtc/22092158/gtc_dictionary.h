#ifndef GTC_DICTIONARY_H
#define GTC_DICTIONARY_H

#include <cstdint>
#include <vector>

struct GtcRule {
    uint32_t left;
    uint32_t right;
};

class GtcDictionary {
public:
    GtcDictionary();

    uint32_t size() const;

    uint32_t addRule(uint32_t left, uint32_t right);

    const GtcRule& rule(uint32_t token) const;

    void clear();

private:
    std::vector<GtcRule> rules_;
};

#endif