#include "gtc_encoder.h"
#include "gtc_decoder.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>


static void printUsage()
{
    std::printf(
        "GTC 0.2 - Grammar Token Compressor\n"
        "\n"
        "Single file:\n"
        "  gtc input.txt output.gtc\n"
        "  gtc -d input.gtc output.txt\n"
        "\n"
        "Archive:\n"
        "  gtc -c archive.gtc file1 file2 ...\n"
        "  gtc -l archive.gtc\n"
        "  gtc -e archive.gtc file output\n"
        "\n");
}


static int encodeSingleFile(
    const char* inputName,
    const char* outputName)
{
    GtcEncoder encoder;
    GtcEncodeStats stats;

    const auto start =
        std::chrono::steady_clock::now();

    if (!encoder.encodeFile(
            inputName,
            outputName,
            stats)) {

        return 1;
    }

    const double ratio =
        stats.originalSize == 0
            ? 0.0
            : static_cast<double>(
                  stats.originalSize) /
              static_cast<double>(
                  stats.compressedBytes);

    const double bpc =
        stats.originalSize == 0
            ? 0.0
            : static_cast<double>(
                  stats.compressedBits) /
              static_cast<double>(
                  stats.originalSize);

    const auto end =
        std::chrono::steady_clock::now();

    const auto elapsed_ms =
        std::chrono::duration_cast<
            std::chrono::milliseconds>(
            end - start).count();

    std::printf(
        "GTC 0.2\n"
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
        static_cast<long long>(
            elapsed_ms));

    return 0;
}


static int encodeArchive(
    int argc,
    char** argv)
{
    /*
     * Syntax:
     *
     *   gtc -c archive.gtc file1 file2 ...
     */
    if (argc <= 3) {

        printUsage();

        return 1;
    }

    const std::string outputName =
        argv[2];

    std::vector<std::string> inputNames;

    for (int i = 3;
         i < argc;
         ++i) {

        inputNames.emplace_back(
            argv[i]);
    }

    GtcEncoder encoder;
    GtcArchiveStats stats;

    const auto start =
        std::chrono::steady_clock::now();

    if (!encoder.encodeArchive(
            inputNames,
            outputName,
            stats)) {

        return 1;
    }

    const double ratio =
        stats.originalSize == 0
            ? 0.0
            : static_cast<double>(
                  stats.originalSize) /
              static_cast<double>(
                  stats.compressedBytes);

    const double bpc =
        stats.originalSize == 0
            ? 0.0
            : static_cast<double>(
                  stats.compressedBits) /
              static_cast<double>(
                  stats.originalSize);

    const auto end =
        std::chrono::steady_clock::now();

    const auto elapsed_ms =
        std::chrono::duration_cast<
            std::chrono::milliseconds>(
            end - start).count();

    std::printf(
        "GTC 0.2 archive\n"
        "\n"
        "files          : %u\n"
        "original       : %llu bytes\n"
        "final tokens   : %llu\n"
        "tokens         : %u\n"
        "grammar rules  : %u\n"
        "Huffman data   : %llu bytes\n"
        "Huffman bits   : %llu\n"
        "ratio          : %.3fx\n"
        "bits/byte      : %.3f\n"
        "time           : \033[32m%lld ms\033[0m\n",
        stats.fileCount,
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
        static_cast<long long>(
            elapsed_ms));

    return 0;
}


static int listArchive(
    const char* archiveName)
{
    GtcDecoder decoder;

    if (!decoder.listArchive(
            archiveName)) {

        return 1;
    }

    return 0;
}


static int extractArchiveFile(
    const char* archiveName,
    const char* fileName,
    const char* outputName)
{
    GtcDecoder decoder;

    if (!decoder.extractArchiveFile(
            archiveName,
            fileName,
            outputName)) {

        return 1;
    }

    std::printf(
        "extracted: %s -> %s\n",
        fileName,
        outputName);

    return 0;
}


int main(
    int argc,
    char** argv)
{
    if (argc < 2) {

        printUsage();

        return 1;
    }

    /*
     * Single-file decode.
     *
     *   gtc -d input.gtc output.txt
     */
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

    /*
     * Create archive.
     *
     *   gtc -c archive.gtc file1 file2 ...
     *
     * Huffman model selection is automatic.
     */
    if (argc >= 4 &&
        std::string(argv[1]) == "-c") {

        return encodeArchive(
            argc,
            argv);
    }

    /*
     * List archive.
     *
     *   gtc -l archive.gtc
     */
    if (argc == 3 &&
        std::string(argv[1]) == "-l") {

        return listArchive(
            argv[2]);
    }

    /*
     * Extract one archive file.
     *
     *   gtc -e archive.gtc file output
     */
    if (argc == 5 &&
        std::string(argv[1]) == "-e") {

        return extractArchiveFile(
            argv[2],
            argv[3],
            argv[4]);
    }

    /*
     * Default: single-file encoder.
     *
     *   gtc input.txt output.gtc
     */
    if (argc == 3) {

        return encodeSingleFile(
            argv[1],
            argv[2]);
    }

    printUsage();

    return 1;
}