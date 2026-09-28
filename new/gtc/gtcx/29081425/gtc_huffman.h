#ifndef GTC_HUFFMAN_H
#define GTC_HUFFMAN_H

#include <cstdint>
#include <vector>

class BitWriter {
public:
    void writeBit(uint32_t bit);
    void writeBits(uint32_t value, uint32_t count);
    void flush();

    const std::vector<uint8_t>& data() const;
    uint64_t bitCount() const;

private:
    std::vector<uint8_t> data_;

    uint8_t current_ = 0;
    uint32_t bits_ = 0;

    uint64_t bit_count_ = 0;
};

class BitReader {
public:
    BitReader(
        const std::vector<uint8_t>& data,
        uint64_t bitCount);

    uint32_t readBit();

    bool seek(uint64_t bitPosition);

    uint64_t position() const;

    uint64_t bitCount() const;

private:
    const std::vector<uint8_t>& data_;

    uint64_t bit_count_;
    uint64_t position_ = 0;
};

class GtcHuffman {
public:
    bool build(
        const std::vector<uint64_t>& frequencies);

    void encode(
        const std::vector<uint32_t>& symbols,
        BitWriter& writer) const;

    bool decode(
        BitReader& reader,
        uint64_t symbolCount,
        std::vector<uint32_t>& output) const;

    const std::vector<uint16_t>& codeLengths() const;

	uint16_t codeLength(
	    uint32_t symbol) const;

    bool buildFromCodeLengths(
        const std::vector<uint16_t>& lengths);

private:
    struct Code {
        std::vector<uint8_t> bits;
    };

    std::vector<Code> codes_;

    std::vector<uint16_t> lengths_;

    struct DecodeNode {
        int child[2];
        int symbol;

        DecodeNode()
            : child{-1, -1},
              symbol(-1)
        {
        }
    };

    std::vector<DecodeNode> decodeTree_;

    bool buildDecodeTree();
};

#endif