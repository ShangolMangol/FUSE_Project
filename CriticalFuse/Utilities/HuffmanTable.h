#ifndef HUFFMAN_TABLE_H
#define HUFFMAN_TABLE_H

#include "BitReader.h"
#include <cstdint>
#include <cstddef>

class HuffmanTable {
public:
    struct Node {
        int symbol = -1;
        Node* left = nullptr;
        Node* right = nullptr;
    };

    HuffmanTable();
    ~HuffmanTable();

    // Parses the Huffman Table from a DHT segment (excluding marker and length)
    bool parse(const uint8_t* data, size_t length);

    // Decode a single symbol from the bitstream
    int decodeSymbol(BitReader& reader) const;

    // Check if table is valid
    bool isValid() const { return root != nullptr; }

    // Clear the table
    void clear();

private:
    Node* root;

    void freeTree(Node* node);
    void insert(uint16_t code, uint8_t length, uint8_t symbol);
};

#endif // HUFFMAN_TABLE_H 