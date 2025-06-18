#include "HuffmanTable.h"
#include <cstring>
#include <iostream>

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

    std::cerr << "HuffmanTable::parse: Table info: 0x" << std::hex << (int)tableInfo << std::dec 
              << ", length: " << length << std::endl;

    uint8_t codeLengths[16];
    std::memcpy(codeLengths, ptr, 16);
    ptr += 16;
    length -= 16;

    std::cerr << "HuffmanTable::parse: Code lengths: ";
    for (int i = 0; i < 16; i++) {
        std::cerr << (int)codeLengths[i] << " ";
    }
    std::cerr << std::endl;

    uint16_t code = 0;
    for (uint8_t i = 0; i < 16; ++i) {
        uint8_t numCodes = codeLengths[i];
        for (uint8_t j = 0; j < numCodes; ++j) {
            if (length == 0) return false;
            uint8_t symbol = *ptr++;
            insert(code, i + 1, symbol);
            std::cerr << "HuffmanTable::parse: Inserted symbol 0x" << std::hex << (int)symbol 
                      << std::dec << " with code " << code << " (length " << (int)(i + 1) << ")" << std::endl;
            code++;
            length--;
        }
        code <<= 1;
    }

    std::cerr << "HuffmanTable::parse: Successfully parsed table" << std::endl;
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
    if (!root) {
        std::cerr << "HuffmanTable::decodeSymbol: No root node" << std::endl;
        return -1;
    }
    
    const Node* node = root;
    int maxDepth = 16; // Safety limit to prevent infinite loops
    int depth = 0;
    
    std::cerr << "HuffmanTable::decodeSymbol: Starting decode, maxDepth: " << maxDepth << std::endl;
    
    while (node && depth < maxDepth) {
        if (node->symbol >= 0) {
            std::cerr << "HuffmanTable::decodeSymbol: Found symbol 0x" << std::hex << node->symbol << std::dec << " at depth " << depth << std::endl;
            return node->symbol;
        }
        uint8_t bit;
        if (!reader.readBit(bit)) {
            std::cerr << "HuffmanTable::decodeSymbol: Failed to read bit at depth " << depth << std::endl;
            return -1;
        }
        std::cerr << "HuffmanTable::decodeSymbol: Read bit " << (int)bit << " at depth " << depth << std::endl;
        node = bit ? node->right : node->left;
        depth++;
    }
    
    std::cerr << "HuffmanTable::decodeSymbol: Failed to decode symbol after " << depth << " bits" << std::endl;
    return -1;
} 