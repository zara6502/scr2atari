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


struct GtcArchiveFile {
    std::string name;

    uint64_t originalSize = 0;

    uint64_t tokenStart = 0;
    uint64_t tokenCount = 0;

    uint64_t bitOffset = 0;
};


struct GtcArchiveStats {
    uint64_t originalSize = 0;
    uint64_t finalTokenCount = 0;

    uint32_t tokenCount = 0;
    uint32_t grammarCount = 0;

    uint64_t compressedBits = 0;
    uint64_t compressedBytes = 0;

    uint32_t fileCount = 0;
};


class GtcEncoder {
public:
    bool encodeFile(
        const std::string& inputName,
        const std::string& outputName,
        GtcEncodeStats& stats);

    /*
     * Archive encoder automatically selects the Huffman model.
     *
     * No user-visible "-p" mode is required.
     */
    bool encodeArchive(
        const std::vector<std::string>& inputNames,
        const std::string& outputName,
        GtcArchiveStats& stats);

private:
    bool readFile(
        const std::string& filename,
        std::vector<uint8_t>& data);

    void buildGrammar(
        const std::vector<uint8_t>& input,
        GtcDictionary& dictionary,
        std::vector<uint32_t>& sequence);

    void buildGrammarMultiFile(
        const std::vector<std::vector<uint8_t>>& files,
        GtcDictionary& dictionary,
        std::vector<uint32_t>& sequence);

    bool writeGtc(
        const std::string& filename,
        uint64_t originalSize,
        const GtcDictionary& dictionary,
        const std::vector<uint32_t>& sequence,
        GtcEncodeStats& stats);

    bool writeArchive(
        const std::string& filename,
        const std::vector<GtcArchiveFile>& files,
        const GtcDictionary& dictionary,
        const std::vector<uint32_t>& sequence,
        GtcArchiveStats& stats);
};

#endif