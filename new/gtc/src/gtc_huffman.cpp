#include "gtc_huffman.h"

#include <algorithm>
#include <queue>

void BitWriter::writeBit(uint32_t bit)
{
    current_ <<= 1;

    if (bit)
        current_ |= 1;

    ++bits_;
    ++bit_count_;

    if (bits_ == 8) {
        data_.push_back(current_);
        current_ = 0;
        bits_ = 0;
    }
}

void BitWriter::writeBits(uint32_t value, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t shift = count - i - 1;
        writeBit((value >> shift) & 1u);
    }
}

const std::vector<uint8_t>& BitWriter::data() const
{
    return data_;
}

uint64_t BitWriter::bitCount() const
{
    return bit_count_;
}

BitReader::BitReader(
    const std::vector<uint8_t>& data,
    uint64_t bitCount)
    : data_(data),
      bit_count_(bitCount)
{
}

uint32_t BitReader::readBit()
{
    if (position_ >= bit_count_)
        return 0;

    const uint64_t bytePos = position_ >> 3;
    const uint32_t bitPos = 7u - static_cast<uint32_t>(position_ & 7u);

    ++position_;

    return (data_[bytePos] >> bitPos) & 1u;
}

namespace {

struct HuffNode {
    uint64_t frequency;
    int symbol;

    HuffNode* left;
    HuffNode* right;

    HuffNode(
        uint64_t f,
        int s,
        HuffNode* l = nullptr,
        HuffNode* r = nullptr)
        : frequency(f),
          symbol(s),
          left(l),
          right(r)
    {
    }
};

struct NodeCompare {
    bool operator()(const HuffNode* a, const HuffNode* b) const
    {
        if (a->frequency != b->frequency)
            return a->frequency > b->frequency;

        return a->symbol > b->symbol;
    }
};

void deleteTree(HuffNode* node)
{
    if (!node)
        return;

    deleteTree(node->left);
    deleteTree(node->right);

    delete node;
}

void makeLengths(
    HuffNode* node,
    uint32_t depth,
    std::vector<uint8_t>& lengths)
{
    if (!node)
        return;

    if (node->symbol >= 0) {
        lengths[static_cast<size_t>(node->symbol)] =
            static_cast<uint8_t>(depth == 0 ? 1 : depth);
        return;
    }

    makeLengths(node->left, depth + 1, lengths);
    makeLengths(node->right, depth + 1, lengths);
}

} // namespace

bool GtcHuffman::build(
    const std::vector<uint64_t>& frequencies)
{
    const size_t count = frequencies.size();

    codes_.assign(count, {});
    lengths_.assign(count, 0);

    std::priority_queue<
        HuffNode*,
        std::vector<HuffNode*>,
        NodeCompare
    > queue;

    for (size_t i = 0; i < count; ++i) {
        if (frequencies[i] != 0) {
            queue.push(
                new HuffNode(
                    frequencies[i],
                    static_cast<int>(i)));
        }
    }

    if (queue.empty())
        return false;

    if (queue.size() == 1) {
        HuffNode* node = queue.top();

        lengths_[static_cast<size_t>(node->symbol)] = 1;

        delete node;

        return buildFromCodeLengths(lengths_);
    }

    while (queue.size() > 1) {
        HuffNode* a = queue.top();
        queue.pop();

        HuffNode* b = queue.top();
        queue.pop();

        int symbol = std::min(a->symbol, b->symbol);

        HuffNode* parent = new HuffNode(
            a->frequency + b->frequency,
            symbol,
            a,
            b);

        queue.push(parent);
    }

    HuffNode* root = queue.top();

    makeLengths(root, 0, lengths_);

    deleteTree(root);

    return buildFromCodeLengths(lengths_);
}

bool GtcHuffman::buildFromCodeLengths(
    const std::vector<uint8_t>& lengths)
{
    lengths_ = lengths;
    codes_.assign(lengths_.size(), {});

    struct Item {
        uint8_t length;
        uint32_t symbol;
    };

    std::vector<Item> items;

    for (uint32_t i = 0; i < lengths_.size(); ++i) {
        if (lengths_[i] != 0) {
            items.push_back({
                lengths_[i],
                i
            });
        }
    }

    if (items.empty())
        return false;

    std::sort(
        items.begin(),
        items.end(),
        [](const Item& a, const Item& b) {
            if (a.length != b.length)
                return a.length < b.length;

            return a.symbol < b.symbol;
        });

    uint32_t code = 0;
    uint8_t previousLength = items[0].length;

    for (size_t i = 0; i < items.size(); ++i) {
        if (i != 0) {
            ++code;

            if (items[i].length > previousLength)
                code <<= (items[i].length - previousLength);
        }

        codes_[items[i].symbol].value = code;
        codes_[items[i].symbol].length = items[i].length;

        previousLength = items[i].length;
    }

    return buildDecodeTree();
}

bool GtcHuffman::buildDecodeTree()
{
    decodeTree_.clear();
    decodeTree_.emplace_back();

    for (uint32_t symbol = 0; symbol < codes_.size(); ++symbol) {
        const Code& code = codes_[symbol];

        if (code.length == 0)
            continue;

        int node = 0;

        for (uint32_t i = 0; i < code.length; ++i) {
            uint32_t shift = code.length - i - 1;
            uint32_t bit = (code.value >> shift) & 1u;

            if (decodeTree_[node].child[bit] == -1) {
                decodeTree_[node].child[bit] =
                    static_cast<int>(decodeTree_.size());

                decodeTree_.emplace_back();
            }

            node = decodeTree_[node].child[bit];
        }

        if (decodeTree_[node].symbol != -1)
            return false;

        decodeTree_[node].symbol = static_cast<int>(symbol);
    }

    return true;
}

void GtcHuffman::encode(
    const std::vector<uint32_t>& symbols,
    BitWriter& writer) const
{
    for (uint32_t symbol : symbols) {
        const Code& code = codes_.at(symbol);

        writer.writeBits(code.value, code.length);
    }
}

bool GtcHuffman::decode(
    BitReader& reader,
    uint64_t symbolCount,
    std::vector<uint32_t>& output) const
{
    output.clear();
    output.reserve(static_cast<size_t>(symbolCount));

    for (uint64_t i = 0; i < symbolCount; ++i) {
        int node = 0;

        while (decodeTree_[node].symbol == -1) {
            uint32_t bit = reader.readBit();

            int next = decodeTree_[node].child[bit];

            if (next < 0)
                return false;

            node = next;
        }

        output.push_back(
            static_cast<uint32_t>(decodeTree_[node].symbol));
    }

    return true;
}

const std::vector<uint8_t>& GtcHuffman::codeLengths() const
{
    return lengths_;
}

void BitWriter::flush()
{
    if (bits_ != 0) {
        current_ <<= (8 - bits_);
        data_.push_back(current_);
        current_ = 0;
        bits_ = 0;
    }
}