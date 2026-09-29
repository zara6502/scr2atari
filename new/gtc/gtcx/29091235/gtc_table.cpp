#include "gtc_table.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

static constexpr uint32_t MODEL_RAW = 0;
static constexpr uint32_t MODEL_CONSTANT = 1;
static constexpr uint32_t MODEL_BITMAP = 2;
static constexpr uint32_t MODEL_SPARSE = 3;
static constexpr uint32_t MODEL_DELTA = 4;
static constexpr uint32_t MODEL_DELTA_SIGNED = 5;

static constexpr uint32_t MODEL_BITS = 3;
static constexpr uint32_t LENGTH_BITS = 13;
static constexpr uint32_t HEADER_BITS =
    MODEL_BITS + LENGTH_BITS;

static constexpr uint32_t BASE_BITS = 5;
static constexpr uint32_t MAX_SEGMENT = 8191;

static constexpr uint64_t INF =
    std::numeric_limits<uint64_t>::max() / 4;


/*
 * Gamma-like code used by the table format.
 *
 * value 0 -> 1 bit
 * value 1 -> 3 bits
 * value 2..3 -> 3 bits
 * value 4..7 -> 5 bits
 * ...
 *
 * It is simply Elias gamma coding of value + 1.
 */
static uint32_t gammaBits(
    uint32_t value)
{
    uint32_t x = value + 1;
    uint32_t bits = 0;

    while (x > 1) {
        x >>= 1;
        ++bits;
    }

    return bits * 2 + 1;
}


static uint32_t zigzag(
    int32_t value)
{
    if (value >= 0)
        return static_cast<uint32_t>(value) * 2u;

    return static_cast<uint32_t>(
        -value) * 2u - 1u;
}


static void writeGamma(
    BitWriter& writer,
    uint32_t value)
{
    const uint32_t x = value + 1;

    uint32_t n = 0;
    uint32_t t = x;

    while (t > 1) {
        t >>= 1;
        ++n;
    }

    for (uint32_t i = 0; i < n; ++i)
        writer.writeBit(0);

    writer.writeBits(
        x,
        n + 1);
}


class TableReader {
public:
    TableReader(
        const std::vector<uint8_t>& data)
        : data_(data)
    {
    }

    bool readBit(
        uint32_t& value)
    {
        if (position_ >=
            static_cast<uint64_t>(data_.size()) * 8ULL)
            return false;

        const uint64_t bytePos =
            position_ >> 3;

        const uint32_t bitPos =
            7u -
            static_cast<uint32_t>(
                position_ & 7u);

        value =
            (data_[bytePos] >> bitPos) & 1u;

        ++position_;

        return true;
    }

    bool readBits(
        uint32_t count,
        uint32_t& value)
    {
        value = 0;

        for (uint32_t i = 0;
             i < count;
             ++i) {

            uint32_t bit;

            if (!readBit(bit))
                return false;

            value =
                (value << 1) | bit;
        }

        return true;
    }

    bool readGamma(
        uint32_t& value)
    {
        uint32_t zeros = 0;

        while (true) {
            uint32_t bit;

            if (!readBit(bit))
                return false;

            if (bit)
                break;

            ++zeros;

            if (zeros > 31)
                return false;
        }

        uint32_t rest = 0;

        if (zeros != 0) {
            if (!readBits(
                    zeros,
                    rest))
                return false;
        }

        const uint32_t x =
            (1u << zeros) | rest;

        value = x - 1;

        return true;
    }

private:
    const std::vector<uint8_t>& data_;
    uint64_t position_ = 0;
};


struct Segment {
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t model = MODEL_RAW;
};



} // namespace


bool GtcLengthTable::encode(
    const std::vector<uint16_t>& lengths,
    std::vector<uint8_t>& output,
    GtcTableStats& stats) const
{
    output.clear();
    stats = {};

    const uint32_t count =
        static_cast<uint32_t>(
            lengths.size());

    if (count == 0)
        return false;

    /*
     * DP over segment boundaries.
     *
     * Segment statistics are updated incrementally
     * as end moves forward. This keeps the DP at
     * O(N^2) instead of O(N^3).
     */
    std::vector<uint64_t> dp(
        static_cast<size_t>(count) + 1,
        INF);

    std::vector<uint32_t> bestStart(
        static_cast<size_t>(count) + 1,
        0);

    std::vector<uint8_t> bestModel(
        static_cast<size_t>(count) + 1,
        0);

    dp[0] = 0;

    for (uint32_t start = 0;
         start < count;
         ++start) {

        if (dp[start] == INF)
            continue;

        const uint32_t base =
            lengths[start];

        if (base > 31)
            continue;

        uint32_t exceptions = 0;

        uint64_t positionBits = 0;
        uint64_t deltaBits = 0;
        uint64_t signedDeltaBits = 0;

        uint32_t lastPosition =
            start - 1;

        bool constant = true;
        bool unsignedValid = true;
        bool valuesValid = true;

        const uint32_t maxEnd =
            std::min(
                count,
                start + MAX_SEGMENT);

        for (uint32_t end =
                 start + 1;
             end <= maxEnd;
             ++end) {

            const uint32_t index =
                end - 1;

            const uint32_t value =
                lengths[index];

            if (value > 31)
                valuesValid = false;

            if (value != base) {

                constant = false;

                const uint32_t positionDelta =
                    index - lastPosition;

                positionBits +=
                    gammaBits(
                        positionDelta);

                lastPosition =
                    index;

                ++exceptions;

                if (value < base) {
                    unsignedValid = false;
                }
                else {
                    deltaBits +=
                        gammaBits(
                            value - base);
                }

                const int32_t signedDelta =
                    static_cast<int32_t>(
                        value) -
                    static_cast<int32_t>(
                        base);

                signedDeltaBits +=
                    gammaBits(
                        zigzag(
                            signedDelta));
            }

            const uint32_t length =
                end - start;

            if (!valuesValid)
                continue;

            /*
             * RAW
             */
            {
                const uint64_t cost =
                    HEADER_BITS +
                    static_cast<uint64_t>(
                        length) *
                    BASE_BITS;

                const uint64_t total =
                    dp[start] + cost;

                if (total < dp[end]) {
                    dp[end] = total;

                    bestStart[end] =
                        start;

                    bestModel[end] =
                        MODEL_RAW;
                }
            }

            /*
             * CONSTANT
             */
            if (constant) {

                const uint64_t cost =
                    HEADER_BITS +
                    BASE_BITS;

                const uint64_t total =
                    dp[start] + cost;

                if (total < dp[end]) {
                    dp[end] = total;

                    bestStart[end] =
                        start;

                    bestModel[end] =
                        MODEL_CONSTANT;
                }
            }

            /*
             * BITMAP
             */
            {
                const uint64_t cost =
                    HEADER_BITS +
                    BASE_BITS +
                    length +
                    static_cast<uint64_t>(
                        exceptions) *
                    BASE_BITS;

                const uint64_t total =
                    dp[start] + cost;

                if (total < dp[end]) {
                    dp[end] = total;

                    bestStart[end] =
                        start;

                    bestModel[end] =
                        MODEL_BITMAP;
                }
            }

            /*
             * SPARSE
             */
            {
                const uint32_t sentinelDelta =
                    end - lastPosition;

                const uint64_t sparseBase =
                    HEADER_BITS +
                    BASE_BITS +
                    positionBits +
                    gammaBits(
                        sentinelDelta);

                const uint64_t cost =
                    sparseBase +
                    static_cast<uint64_t>(
                        exceptions) *
                    BASE_BITS;

                const uint64_t total =
                    dp[start] + cost;

                if (total < dp[end]) {
                    dp[end] = total;

                    bestStart[end] =
                        start;

                    bestModel[end] =
                        MODEL_SPARSE;
                }
            }

            /*
             * DELTA-SPARSE
             */
            if (unsignedValid) {

                const uint32_t sentinelDelta =
                    end - lastPosition;

                const uint64_t sparseBase =
                    HEADER_BITS +
                    BASE_BITS +
                    positionBits +
                    gammaBits(
                        sentinelDelta);

                const uint64_t cost =
                    sparseBase +
                    deltaBits;

                const uint64_t total =
                    dp[start] + cost;

                if (total < dp[end]) {
                    dp[end] = total;

                    bestStart[end] =
                        start;

                    bestModel[end] =
                        MODEL_DELTA;
                }
            }

            /*
             * DELTA-SPARSE-SIGNED
             */
            {
                const uint32_t sentinelDelta =
                    end - lastPosition;

                const uint64_t sparseBase =
                    HEADER_BITS +
                    BASE_BITS +
                    positionBits +
                    gammaBits(
                        sentinelDelta);

                const uint64_t cost =
                    sparseBase +
                    signedDeltaBits;

                const uint64_t total =
                    dp[start] + cost;

                if (total < dp[end]) {
                    dp[end] = total;

                    bestStart[end] =
                        start;

                    bestModel[end] =
                        MODEL_DELTA_SIGNED;
                }
            }
        }
    }

    if (dp[count] == INF)
        return false;

    /*
     * Reconstruct segments.
     */
    std::vector<Segment> segments;

    uint32_t end = count;

    while (end != 0) {
        Segment segment;

        segment.start =
            bestStart[end];

        segment.end =
            end;

        segment.model =
            bestModel[end];

        segments.push_back(
            segment);

        end =
            segment.start;
    }

    std::reverse(
        segments.begin(),
        segments.end());

    BitWriter writer;

    for (const Segment& segment :
         segments) {

        const uint32_t start =
            segment.start;

        const uint32_t endPos =
            segment.end;

        const uint32_t length =
            endPos - start;

        const uint32_t model =
            segment.model;

        writer.writeBits(
            model,
            MODEL_BITS);

        writer.writeBits(
            length,
            LENGTH_BITS);

        if (model == MODEL_RAW) {

            for (uint32_t i = start;
                 i < endPos;
                 ++i) {

                writer.writeBits(
                    lengths[i],
                    BASE_BITS);
            }

            continue;
        }

        const uint32_t base =
            lengths[start];

        writer.writeBits(
            base,
            BASE_BITS);

        if (model == MODEL_CONSTANT)
            continue;

        if (model == MODEL_BITMAP) {

            for (uint32_t i = start;
                 i < endPos;
                 ++i) {

                writer.writeBit(
                    lengths[i] != base);
            }

            for (uint32_t i = start;
                 i < endPos;
                 ++i) {

                if (lengths[i] != base) {
                    writer.writeBits(
                        lengths[i],
                        BASE_BITS);
                }
            }

            continue;
        }

        /*
         * Sparse models.
         */
        uint32_t lastPosition =
            start - 1;

        for (uint32_t i = start;
             i < endPos;
             ++i) {

            if (lengths[i] == base)
                continue;

            const uint32_t delta =
                i - lastPosition;

            writeGamma(
                writer,
                delta);

            lastPosition = i;

            if (model == MODEL_DELTA) {

                const uint32_t value =
                    lengths[i];

                if (value < base)
                    return false;

                writeGamma(
                    writer,
                    value - base);
            }
            else if (
                model ==
                MODEL_DELTA_SIGNED) {

                const int32_t deltaValue =
                    static_cast<int32_t>(
                        lengths[i]) -
                    static_cast<int32_t>(
                        base);

                writeGamma(
                    writer,
                    zigzag(deltaValue));
            }
            else {
                writer.writeBits(
                    lengths[i],
                    BASE_BITS);
            }
        }

        /*
         * Sentinel.
         *
         * It moves the position exactly to
         * the end of the segment.
         */
        writeGamma(
            writer,
            endPos - lastPosition);
    }

    writer.flush();

    output =
        writer.data();

    stats.bitCount =
        writer.bitCount();

    stats.byteCount =
        static_cast<uint32_t>(
            output.size());

    stats.segmentCount =
        static_cast<uint32_t>(
            segments.size());

    return true;
}

bool GtcLengthTable::decode(
    const std::vector<uint8_t>& input,
    uint32_t tokenCount,
    std::vector<uint16_t>& lengths) const
{
    lengths.clear();
    lengths.reserve(tokenCount);

    if (tokenCount == 0)
        return false;

    TableReader reader(input);

    while (lengths.size() < tokenCount) {

        uint32_t model;
        uint32_t length;

        if (!reader.readBits(
                MODEL_BITS,
                model))
            return false;

        if (!reader.readBits(
                LENGTH_BITS,
                length))
            return false;

        if (length == 0 ||
            length > MAX_SEGMENT)
            return false;

        if (lengths.size() + length >
            tokenCount)
            return false;

        if (model > MODEL_DELTA_SIGNED)
            return false;

        if (model == MODEL_RAW) {

            for (uint32_t i = 0;
                 i < length;
                 ++i) {

                uint32_t value;

                if (!reader.readBits(
                        BASE_BITS,
                        value))
                    return false;

                lengths.push_back(
                    static_cast<uint16_t>(
                        value));
            }

            continue;
        }

        uint32_t base;

        if (!reader.readBits(
                BASE_BITS,
                base))
            return false;

        if (model == MODEL_CONSTANT) {

            for (uint32_t i = 0;
                 i < length;
                 ++i) {

                lengths.push_back(
                    static_cast<uint16_t>(
                        base));
            }

            continue;
        }

        if (model == MODEL_BITMAP) {

            std::vector<uint8_t> bitmap(
                length);

            for (uint32_t i = 0;
                 i < length;
                 ++i) {

                uint32_t bit;

                if (!reader.readBit(bit))
                    return false;

                bitmap[i] =
                    static_cast<uint8_t>(
                        bit);
            }

            for (uint32_t i = 0;
                 i < length;
                 ++i) {

                if (!bitmap[i]) {
                    lengths.push_back(
                        static_cast<uint16_t>(
                            base));
                    continue;
                }

                uint32_t value;

                if (!reader.readBits(
                        BASE_BITS,
                        value))
                    return false;

                lengths.push_back(
                    static_cast<uint16_t>(
                        value));
            }

            continue;
        }

        /*
         * Sparse / delta-sparse.
         */
        std::vector<uint16_t> segment(
            length,
            static_cast<uint16_t>(base));

        uint32_t previous =
            0;

        bool first = true;

        while (true) {

            uint32_t delta;

            if (!reader.readGamma(delta))
                return false;

            if (delta == 0)
                return false;

            uint64_t position;

            if (first)
                position =
                    static_cast<uint64_t>(
                        delta) - 1ULL;
            else
                position =
                    static_cast<uint64_t>(
                        previous) +
                    delta;

            /*
             * End sentinel.
             */
            if (position == length)
                break;

            if (position >= length)
                return false;

            previous =
                static_cast<uint32_t>(
                    position);

            first = false;

            uint32_t value;

            if (model == MODEL_SPARSE) {

                if (!reader.readBits(
                        BASE_BITS,
                        value))
                    return false;

                segment[
                    static_cast<size_t>(
                        position)] =
                    static_cast<uint16_t>(
                        value);
            }
            else if (
                model == MODEL_DELTA) {

                if (!reader.readGamma(
                        value))
                    return false;

                const uint32_t result =
                    base + value;

                if (result > 31)
                    return false;

                segment[
                    static_cast<size_t>(
                        position)] =
                    static_cast<uint16_t>(
                        result);
            }
            else {
                uint32_t encoded;

                if (!reader.readGamma(
                        encoded))
                    return false;

                const int32_t deltaValue =
                    (encoded & 1u)
                        ? -static_cast<int32_t>(
                              (encoded + 1u) >> 1)
                        : static_cast<int32_t>(
                              encoded >> 1);

                const int32_t result =
                    static_cast<int32_t>(
                        base) +
                    deltaValue;

                if (result < 0 ||
                    result > 31)
                    return false;

                segment[
                    static_cast<size_t>(
                        position)] =
                    static_cast<uint16_t>(
                        result);
            }
        }

        lengths.insert(
            lengths.end(),
            segment.begin(),
            segment.end());
    }

    return lengths.size() ==
           tokenCount;
}
