#ifndef BIT_READER_H
#define BIT_READER_H

#include <cstdint>
#include <cstddef>

class BitReader {
private:
    const uint8_t* buffer;
    size_t size;
    size_t bytePos;
    int bitPos; // 0-7
    bool isLittleEndian;
    bool msbFirst; // true for MSB-first (big endian), false for LSB-first (little endian)

public:
    BitReader(const uint8_t* buf, size_t len, bool msbFirst = true)
        : buffer(buf), size(len), bytePos(0), bitPos(0), msbFirst(msbFirst) {
        // Detect system endianness
        union {
            uint16_t value;
            uint8_t bytes[2];
        } test = {0x0102};
        isLittleEndian = (test.bytes[0] == 0x02);
    }

    // Check if end of stream
    bool eof() const {
        return bytePos >= size;
    }

    // Read one bit
    bool readBit(uint8_t& bit) {
        if (eof()) return false;
        
        if (msbFirst) {
            // MSB-first (big endian) - read from left to right
            bit = (buffer[bytePos] >> (7 - bitPos)) & 1;
        } else {
            // LSB-first (little endian) - read from right to left
            bit = (buffer[bytePos] >> bitPos) & 1;
        }
        
        bitPos++;
        if (bitPos == 8) {
            bitPos = 0;
            bytePos++;
        }
        return true;
    }

    // Read multiple bits (up to 32 bits) with proper endianness
    bool readBits(uint32_t& out, int count) {
        if (count <= 0 || count > 32) return false;
        out = 0;
        
        if (msbFirst) {
            // MSB-first: read bits from left to right
            for (int i = 0; i < count; ++i) {
                uint8_t b;
                if (!readBit(b)) return false;
                out = (out << 1) | b;
            }
        } else {
            // LSB-first: read bits from right to left
            for (int i = 0; i < count; ++i) {
                uint8_t b;
                if (!readBit(b)) return false;
                out |= (b << i);
            }
        }
        return true;
    }

    // Read 16-bit value with proper endianness handling
    bool readUint16(uint16_t& out) {
        if (bytePos + 1 >= size) return false;
        
        if (isLittleEndian) {
            out = (buffer[bytePos + 1] << 8) | buffer[bytePos];
        } else {
            out = (buffer[bytePos] << 8) | buffer[bytePos + 1];
        }
        
        bytePos += 2;
        bitPos = 0; // Reset bit position after reading full bytes
        return true;
    }

    // Read 32-bit value with proper endianness handling
    bool readUint32(uint32_t& out) {
        if (bytePos + 3 >= size) return false;
        
        if (isLittleEndian) {
            out = (buffer[bytePos + 3] << 24) | (buffer[bytePos + 2] << 16) | 
                  (buffer[bytePos + 1] << 8) | buffer[bytePos];
        } else {
            out = (buffer[bytePos] << 24) | (buffer[bytePos + 1] << 16) | 
                  (buffer[bytePos + 2] << 8) | buffer[bytePos + 3];
        }
        
        bytePos += 4;
        bitPos = 0; // Reset bit position after reading full bytes
        return true;
    }

    // Skip bits without reading
    bool skipBits(int count) {
        for (int i = 0; i < count; ++i) {
            uint8_t dummy;
            if (!readBit(dummy)) return false;
        }
        return true;
    }

    // Skip to next byte boundary
    bool skipToByteBoundary() {
        if (bitPos == 0) return true; // Already at byte boundary
        
        int bitsToSkip = 8 - bitPos;
        return skipBits(bitsToSkip);
    }

    // Get current byte offset
    size_t getByteOffset() const {
        return bytePos;
    }

    // Get current bit offset inside byte
    int getBitOffset() const {
        return bitPos;
    }

    // Get absolute bit position (useful for debugging)
    size_t getAbsoluteBitPosition() const {
        return bytePos * 8 + bitPos;
    }

    // Check if system is little endian
    bool isSystemLittleEndian() const {
        return isLittleEndian;
    }

    // Check if reading MSB-first
    bool isMSBFirst() const {
        return msbFirst;
    }

    // Set bit reading order
    void setBitOrder(bool msbFirst) {
        this->msbFirst = msbFirst;
    }
};

#endif // BIT_READER_H 