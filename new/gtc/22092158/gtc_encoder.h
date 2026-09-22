#ifndef GTC_ENCODER_H
#define GTC_ENCODER_H

#include "gtc_dictionary.h"

#include <cstdint>
#include <string>
#include <vector>

struct GtcEncodeStats {
    uint64_t originalSize = 0;
    uint64_t finalTokenCount = 0;
    uint32_t tokenCount = 0;
    uint32_t grammarCount = 0;
    uint64_t compressedBits = 0;
    uint64_t compressedBytes = 0;
};

class GtcEncoder {
public:
    bool encodeFile(
        const std::string& inputName,
        const std::string& outputName,
        GtcEncodeStats& stats);

private:
    bool readFile(
        const std::string& filename,
        std::vector<uint8_t>& data);

    bool writeFile(
        const std::string& filename,
        const std::vector<uint8_t>& data);

    void buildGrammar(
        const std::vector<uint8_t>& input,
        GtcDictionary& dictionary,
        std::vector<uint32_t>& sequence);

    bool writeGtc(
        const std::string& filename,
        uint64_t originalSize,
        const GtcDictionary& dictionary,
        const std::vector<uint32_t>& sequence,
        GtcEncodeStats& stats);
};

#endif