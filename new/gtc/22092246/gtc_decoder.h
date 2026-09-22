#ifndef GTC_DECODER_H
#define GTC_DECODER_H

#include <cstdint>
#include <string>
#include <vector>

class GtcDecoder {
public:
    bool decodeFile(
        const std::string& inputName,
        const std::string& outputName);

private:
    bool readU32(
        const std::vector<uint8_t>& data,
        size_t& pos,
        uint32_t& value);

    bool readU64(
        const std::vector<uint8_t>& data,
        size_t& pos,
        uint64_t& value);
};

#endif