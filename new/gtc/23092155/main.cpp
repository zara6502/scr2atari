#include "gtc_encoder.h"
#include "gtc_decoder.h"

#include <cstdio>
#include <string>
#include <chrono>

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

    const auto start = std::chrono::steady_clock::now();

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

    const auto end = std::chrono::steady_clock::now();

    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            end - start).count();

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
        "bits/byte      : %.3f\n"
	"time           : \033[32m%lld ms\033[0m\n",
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
        bpc,
	elapsed_ms);

    return 0;
}