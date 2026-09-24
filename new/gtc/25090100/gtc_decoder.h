#ifndef GTC_DECODER_H
#define GTC_DECODER_H

#include <cstdint>
#include <string>
#include <vector>


struct GtcArchiveEntry {
    std::string name;

    uint64_t originalSize = 0;

    uint64_t tokenStart = 0;
    uint64_t tokenCount = 0;

    uint64_t bitOffset = 0;
};


class GtcDecoder {
public:
    bool decodeFile(
        const std::string& inputName,
        const std::string& outputName);

    bool listArchive(
        const std::string& archiveName);

    bool extractArchiveFile(
        const std::string& archiveName,
        const std::string& fileName,
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