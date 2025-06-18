#include "HuffmanTable.h"
#include <cstring>

HuffmanTable::HuffmanTable() {
    root = new Node();
}

HuffmanTable::~HuffmanTable() {
    freeTree(root);
}

void HuffmanTable::clear() {
    freeTree(root);
    root = new Node();
}

void HuffmanTable::freeTree(Node* node) {
    if (!node) return;
    freeTree(node->left);
    freeTree(node->right);
    delete node;
}

// Parse the Huffman table from JPEG DHT format
bool HuffmanTable::parse(const uint8_t* data, size_t length) {
    if (length < 17) return false; // At least 1 byte for class/id and 16 for lengths

    const uint8_t* ptr = data;
    uint8_t tableInfo = *ptr++; // upper 4 bits: class (0 = DC, 1 = AC), lower 4 bits: id
    length--;

    uint8_t codeLengths[16];
    std::memcpy(codeLengths, ptr, 16);
    ptr += 16;
    length -= 16;

    uint16_t code = 0;
    for (uint8_t i = 0; i < 16; ++i) {
        uint8_t numCodes = codeLengths[i];
        for (uint8_t j = 0; j < numCodes; ++j) {
            if (length == 0) return false;
            uint8_t symbol = *ptr++;
            insert(code, i + 1, symbol);
            code++;
            length--;
        }
        code <<= 1;
    }

    return true;
}

// Insert a symbol with a binary Huffman code of a given length
void HuffmanTable::insert(uint16_t code, uint8_t length, uint8_t symbol) {
    Node* node = root;
    for (int i = length - 1; i >= 0; --i) {
        bool bit = (code >> i) & 1;
        if (bit) {
            if (!node->right) node->right = new Node();
            node = node->right;
        } else {
            if (!node->left) node->left = new Node();
            node = node->left;
        }
    }
    node->symbol = symbol;
}

// Decode one Huffman symbol from the bitstream
int HuffmanTable::decodeSymbol(BitReader& reader) const {
    const Node* node = root;
    while (node) {
        if (node->symbol >= 0) return node->symbol;
        uint8_t bit;
        if (!reader.readBit(bit)) return -1;
        node = bit ? node->right : node->left;
    }
    return -1;
} 