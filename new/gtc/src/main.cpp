#include "gtc_encoder.h"
#include "gtc_decoder.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

static void printUsage()
{
    std::printf(
        "GTC 0.1 - Grammar Token Compressor\n"
        "\n"
        "Usage:\n"
        "  gtc input.txt output.gtc\n"
        "  gtc -d input.gtc output.txt\n"
        "\n");
}

int main(int argc, char** argv)
{
    if (argc == 4 &&
        std::string(argv[1]) == "-d") {

        GtcDecoder decoder;

        if (!decoder.decodeFile(
                argv[2],
                argv[3])) {

            return 1;
        }

        std::printf(
            "decoded: %s -> %s\n",
            argv[2],
            argv[3]);

        return 0;
    }

    if (argc != 3) {
        printUsage();
        return 1;
    }

    GtcEncoder encoder;
    GtcEncodeStats stats;

    if (!encoder.encodeFile(
            argv[1],
            argv[2],
            stats)) {

        return 1;
    }

    const double ratio =
        stats.originalSize == 0
            ? 0.0
            : static_cast<double>(stats.originalSize) /
              static_cast<double>(
                  stats.compressedBytes);

    const double bpc =
        stats.originalSize == 0
            ? 0.0
            : static_cast<double>(
                  stats.compressedBits) /
              static_cast<double>(
                  stats.originalSize);

    std::printf(
        "GTC 0.1\n"
        "\n"
        "original       : %llu bytes\n"
        "final tokens   : %llu\n"
        "tokens         : %u\n"
        "grammar rules  : %u\n"
        "Huffman data   : %llu bytes\n"
        "Huffman bits   : %llu\n"
        "ratio          : %.3fx\n"
        "bits/byte      : %.3f\n",
        static_cast<unsigned long long>(
            stats.originalSize),
        static_cast<unsigned long long>(
            stats.finalTokenCount),
        stats.tokenCount,
        stats.grammarCount,
        static_cast<unsigned long long>(
            stats.compressedBytes),
        static_cast<unsigned long long>(
            stats.compressedBits),
        ratio,
        bpc);

    return 0;
}