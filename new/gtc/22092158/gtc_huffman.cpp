#include <iostream>
#include <functional>
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

void BitWriter::writeBits(
    uint32_t value,
    uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t shift =
            count - i - 1;

        writeBit(
            (value >> shift) & 1u);
    }
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

const std::vector<uint8_t>&
BitWriter::data() const
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

    const uint64_t bytePos =
        position_ >> 3;

    const uint32_t bitPos =
        7u -
        static_cast<uint32_t>(
            position_ & 7u);

    ++position_;

    return
        (data_[bytePos] >> bitPos) & 1u;
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
    bool operator()(
        const HuffNode* a,
        const HuffNode* b) const
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
    std::vector<uint16_t>& lengths)
{
    if (!node)
        return;

    const bool isLeaf =
        node->left == nullptr &&
        node->right == nullptr;

    if (isLeaf) {
        if (depth == 0)
            depth = 1;

        if (depth > 65535u)
            depth = 65535u;

        lengths[
            static_cast<size_t>(
                node->symbol)] =
            static_cast<uint16_t>(depth);

        return;
    }

    makeLengths(
        node->left,
        depth + 1,
        lengths);

    makeLengths(
        node->right,
        depth + 1,
        lengths);
}

bool incrementBits(
    std::vector<uint8_t>& bits)
{
    /*
     * Increment a binary number represented
     * MSB -> LSB.
     *
     * Example:
     *
     * 00111 -> 01000
     */
    for (size_t i = bits.size(); i != 0; --i) {
        const size_t index = i - 1;

        if (bits[index] == 0) {
            bits[index] = 1;
            return true;
        }

        bits[index] = 0;
    }

    /*
     * Carry beyond the current code length.
     */
    return false;
}


} // namespace


bool GtcHuffman::build(
    const std::vector<uint64_t>& frequencies)
{
    const size_t count =
        frequencies.size();

    codes_.assign(
        count,
        Code{});

    lengths_.assign(
        count,
        0);

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

    /*
     * Special case:
     * only one symbol.
     */
    if (queue.size() == 1) {
        HuffNode* node =
            queue.top();

        lengths_[
            static_cast<size_t>(
                node->symbol)] = 1;

        delete node;

        return buildFromCodeLengths(
            lengths_);
    }
std::cerr
    << "\nQueue diagnostic:\n"
    << "  initial nodes : "
    << queue.size()
    << "\n";

    while (queue.size() > 1) {
        HuffNode* a =
            queue.top();

        queue.pop();

        HuffNode* b =
            queue.top();

        queue.pop();

        /*
         * The symbol value is only used
         * as a deterministic tie-breaker.
         *
         * Internal nodes therefore get the
         * smallest symbol below them.
         */
        const int symbol =
            std::min(
                a->symbol,
                b->symbol);

        HuffNode* parent =
            new HuffNode(
                a->frequency +
                    b->frequency,
                symbol,
                a,
                b);

        queue.push(parent);
    }
std::cerr
    << "  final nodes   : "
    << queue.size()
    << "\n";

    HuffNode* root =
        queue.top();
std::cerr
    << "\nRoot diagnostic:\n"
    << "  symbol : "
    << root->symbol
    << "\n"
    << "  freq   : "
    << root->frequency
    << "\n"
    << "  left   : "
    << (root->left != nullptr)
    << "\n"
    << "  right  : "
    << (root->right != nullptr)
    << "\n";
uint64_t leafCount = 0;
uint64_t nodeCount = 0;

std::function<void(HuffNode*)> countTree =
    [&](HuffNode* node)
{
    if (!node)
        return;

    ++nodeCount;

    if (node->left == nullptr &&
        node->right == nullptr) {
        ++leafCount;
        return;
    }

    countTree(node->left);
    countTree(node->right);
};

countTree(root);

std::cerr
    << "\nTree diagnostic:\n"
    << "  nodes : "
    << nodeCount
    << "\n"
    << "  leaves: "
    << leafCount
    << "\n";
makeLengths(root, 0, lengths_);

uint32_t used = 0;

for (uint16_t length : lengths_) {
    if (length != 0)
        ++used;
}

std::cerr
    << "\nLengths diagnostic:\n"
    << "  non-zero lengths : "
    << used
    << "\n";

uint16_t minLength = 65535;
uint16_t maxLength = 0;

for (uint16_t length : lengths_) {
    if (length == 0)
        continue;

    ++used;

    if (length < minLength)
        minLength = length;

    if (length > maxLength)
        maxLength = length;
}

std::cerr
    << "\nHuffman build diagnostic:\n"
    << "  frequencies used : "
    << used
    << "\n"
    << "  min length       : "
    << minLength
    << "\n"
    << "  max length       : "
    << maxLength
    << "\n";

deleteTree(root);

return buildFromCodeLengths(lengths_);
}


bool GtcHuffman::buildFromCodeLengths(
    const std::vector<uint16_t>& lengths)
{
    lengths_ = lengths;

    uint32_t debugUsed = 0;

    for (uint16_t length : lengths_) {
        if (length != 0)
            ++debugUsed;
    }

    std::cerr
        << "\nInside buildFromCodeLengths:\n"
        << "  input lengths : "
        << lengths.size()
        << "\n"
        << "  non-zero      : "
        << debugUsed
        << "\n";

    codes_.assign(
        lengths_.size(),
        Code{});

    struct Item {
        uint16_t length;
        uint32_t symbol;
    };

    std::vector<Item> items;

    for (uint32_t i = 0;
         i < lengths_.size();
         ++i) {

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
        [](const Item& a,
           const Item& b) {

            if (a.length != b.length)
                return a.length < b.length;

            return a.symbol < b.symbol;
        });

    /*
     * Canonical Huffman code.
     *
     * Unlike the old implementation, the code
     * is represented as an arbitrary-length
     * vector of bits. There is no uint32_t
     * overflow.
     */
    std::vector<uint8_t> code(
        items[0].length,
        0);

    uint16_t previousLength =
        items[0].length;

    for (size_t i = 0;
         i < items.size();
         ++i) {

        const uint16_t length =
            items[i].length;

        if (length == 0)
            return false;

        if (i != 0) {
            /*
             * Increment the previous canonical
             * code.
             */
            if (!incrementBits(code))
                return false;

            /*
             * Move to a longer code by appending
             * zero bits.
             */
            if (length < previousLength)
                return false;

            code.resize(
                length,
                0);
        }

        /*
         * Store a copy of the canonical code.
         */
        codes_[
            items[i].symbol].bits = code;

        previousLength = length;
    }

    /*
     * Validate the resulting canonical codes
     * by constructing the decoder tree.
     */
    return buildDecodeTree();
}


bool GtcHuffman::buildDecodeTree()
{
    decodeTree_.clear();

    decodeTree_.emplace_back();

    for (uint32_t symbol = 0;
         symbol < codes_.size();
         ++symbol) {

        const Code& code =
            codes_[symbol];

        if (code.bits.empty())
            continue;

        int node = 0;

        for (size_t i = 0;
             i < code.bits.size();
             ++i) {

            const uint32_t bit =
                code.bits[i] & 1u;

            /*
             * A node which already represents
             * a complete symbol cannot have
             * another symbol below it.
             */
            if (decodeTree_[node].symbol != -1)
                return false;

            if (decodeTree_[node].child[bit] == -1) {
                decodeTree_[node].child[bit] =
                    static_cast<int>(
                        decodeTree_.size());

                decodeTree_.emplace_back();
            }

            node =
                decodeTree_[node].child[bit];
        }

        /*
         * Two symbols must not have the same
         * canonical code.
         */
        if (decodeTree_[node].symbol != -1)
            return false;

        /*
         * A complete symbol cannot be a prefix
         * of another symbol.
         */
        if (decodeTree_[node].child[0] != -1 ||
            decodeTree_[node].child[1] != -1)
            return false;

        decodeTree_[node].symbol =
            static_cast<int>(symbol);
    }

    return true;
}


void GtcHuffman::encode(
    const std::vector<uint32_t>& symbols,
    BitWriter& writer) const
{
    for (uint32_t symbol : symbols) {
        if (symbol >= codes_.size())
            continue;

        const Code& code =
            codes_[symbol];

        for (uint8_t bit : code.bits)
            writer.writeBit(bit);
    }
}


bool GtcHuffman::decode(
    BitReader& reader,
    uint64_t symbolCount,
    std::vector<uint32_t>& output) const
{
    output.clear();

    output.reserve(
        static_cast<size_t>(
            symbolCount));

    if (decodeTree_.empty())
        return false;

    for (uint64_t i = 0;
         i < symbolCount;
         ++i) {

        int node = 0;

        while (
            decodeTree_[node].symbol == -1) {

            const uint32_t bit =
                reader.readBit();

            const int next =
                decodeTree_[node].child[bit];

            if (next < 0)
                return false;

            node = next;
        }

        output.push_back(
            static_cast<uint32_t>(
                decodeTree_[node].symbol));
    }

    return true;
}


const std::vector<uint16_t>&
GtcHuffman::codeLengths() const
{
    return lengths_;
}