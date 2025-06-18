#include "JpegFile.h"
#include "../Utilities/BitReader.h"
#include "../Utilities/HuffmanTable.h"
#include <cstdint>
#include <cstring>
#include <vector>
#include <map>
#include <iostream>
#include <fstream>

// JPEG marker constants
const uint8_t SOI_MARKER = 0xD8;
const uint8_t EOI_MARKER = 0xD9;
const uint8_t SOS_MARKER = 0xDA;
const uint8_t DHT_MARKER = 0xC4;

// Structure to store exact coefficient information
struct ExactCoefficient {
    bool isDC;
    size_t byteStart;
    size_t byteEnd;
    int16_t value;
    uint8_t runLength; // for AC coefficients
    uint8_t sizeBits;  // number of bits for amplitude
};

// Global storage for exact coefficient separation
static std::vector<ExactCoefficient> exactCoefficients;
static std::vector<uint8_t> criticalData;
static std::vector<int16_t> acCoefficientValues; // Store actual coefficient values
static std::vector<uint8_t> acCoefficientRawData; // Store raw entropy data
static size_t originalSize = 0;

// Read 16-bit value from JPEG data (JPEG uses big-endian for segment lengths)
uint16_t readJPEGUint16(const uint8_t* data) {
    return (data[0] << 8) | data[1]; // Big-endian (network byte order)
}

// Parse DHT (Define Huffman Table) segments
bool parseDHT(const uint8_t* data, size_t length, std::map<uint8_t, HuffmanTable>& tables) {
    if (length < 2) return false;
    
    uint16_t dhtLength = readJPEGUint16(data);
    if (dhtLength < 3 || dhtLength > length) return false;
    
    size_t pos = 2;
    while (pos < dhtLength) {
        if (pos + 1 >= dhtLength) break;
        
        uint8_t tableInfo = data[pos];
        uint8_t tableClass = (tableInfo >> 4) & 0x0F; // 0=DC, 1=AC
        uint8_t tableId = tableInfo & 0x0F;
        uint8_t tableKey = (tableClass << 4) | tableId;
        
        pos++;
        
        HuffmanTable table;
        if (!table.parse(data + pos, dhtLength - pos)) return false;
        tables[tableKey] = table;
        
        // Calculate how much data was consumed
        uint8_t codeLengths[16];
        std::memcpy(codeLengths, data + pos, 16);
        pos += 16;
        
        int totalSymbols = 0;
        for (int i = 0; i < 16; i++) {
            totalSymbols += codeLengths[i];
        }
        pos += totalSymbols;
    }
    
    return true;
}

// Parse SOS (Start of Scan) and decode coefficients exactly
bool parseSOSAndCoefficients(const uint8_t* data, size_t length, size_t baseOffset, 
                           const std::map<uint8_t, HuffmanTable>& tables) {
    if (length < 2) return false;
    
    uint16_t sosLength = readJPEGUint16(data);
    if (sosLength < 2 || sosLength > length) return false;
    
    // Copy SOS header to critical data
    criticalData.insert(criticalData.end(), data, data + sosLength);
    
    // Parse SOS header to find component info and Huffman table assignments
    if (sosLength < 6) return false; // Minimum SOS header size
    
    uint8_t numComponents = data[2];
    if (sosLength < 6 + numComponents * 2) return false;
    
    // Parse component info and find Huffman tables
    std::map<uint8_t, uint8_t> componentToDCTable;
    std::map<uint8_t, uint8_t> componentToACTable;
    
    for (int i = 0; i < numComponents; i++) {
        uint8_t componentId = data[3 + i * 2];
        uint8_t tableSelectors = data[4 + i * 2];
        uint8_t dcTableId = (tableSelectors >> 4) & 0x0F;
        uint8_t acTableId = tableSelectors & 0x0F;
        
        componentToDCTable[componentId] = dcTableId;
        componentToACTable[componentId] = acTableId;
    }
    
    // Find the Huffman tables we need
    HuffmanTable dcTable, acTable;
    bool dcTableFound = false, acTableFound = false;
    
    for (const auto& pair : tables) {
        uint8_t tableClass = (pair.first >> 4) & 0x0F;
        uint8_t tableId = pair.first & 0x0F;
        
        // Check if this table matches any component's requirements
        for (const auto& compPair : componentToDCTable) {
            if (tableClass == 0 && tableId == compPair.second) { // DC table
                dcTable = pair.second;
                dcTableFound = true;
            }
        }
        
        for (const auto& compPair : componentToACTable) {
            if (tableClass == 1 && tableId == compPair.second) { // AC table
                acTable = pair.second;
                acTableFound = true;
            }
        }
    }
    
    if (!dcTableFound || !acTableFound) {
        std::cerr << "Required Huffman tables not found" << std::endl;
        return false;
    }
    
    // Start decoding entropy-coded data
    const uint8_t* entropyData = data + sosLength;
    size_t entropyLength = length - sosLength;
    
    // JPEG uses MSB-first bit reading (big endian bit order)
    BitReader reader(entropyData, entropyLength, true); // true = MSB-first
    
    size_t blockCount = 0;
    const size_t maxBlocks = 10000; // Safety limit
    
    while (!reader.eof() && blockCount < maxBlocks) {
        size_t posBefore = reader.getByteOffset();
        
        // Each 8x8 block has 1 DC + 63 AC coefficients
        // For DC coefficient (first in block)
        if (blockCount % 64 == 0) {
            ExactCoefficient dc;
            dc.isDC = true;
            dc.byteStart = baseOffset + posBefore;
            
            // Decode DC coefficient using DC table
            int symbol = dcTable.decodeSymbol(reader);
            if (symbol < 0) break;
            
            dc.sizeBits = symbol & 0x0F;
            if (dc.sizeBits > 0) {
                uint32_t amplitude;
                if (!reader.readBits(amplitude, dc.sizeBits)) break;
                dc.value = static_cast<int16_t>(amplitude);
            } else {
                dc.value = 0;
            }
            
            dc.byteEnd = baseOffset + reader.getByteOffset() - 1;
            exactCoefficients.push_back(dc);
        } else {
            // AC coefficient
            ExactCoefficient ac;
            ac.isDC = false;
            ac.byteStart = baseOffset + posBefore;
            
            // Decode AC coefficient using AC table
            int symbol = acTable.decodeSymbol(reader);
            if (symbol < 0) break;
            
            if (symbol == 0x00) {
                // End of block
                ac.runLength = 0;
                ac.sizeBits = 0;
                ac.value = 0;
            } else {
                ac.runLength = (symbol >> 4) & 0x0F;
                ac.sizeBits = symbol & 0x0F;
                
                if (ac.sizeBits > 0) {
                    uint32_t amplitude;
                    if (!reader.readBits(amplitude, ac.sizeBits)) break;
                    ac.value = static_cast<int16_t>(amplitude);
                } else {
                    ac.value = 0;
                }
            }
            
            ac.byteEnd = baseOffset + reader.getByteOffset() - 1;
            exactCoefficients.push_back(ac);
            
            // Store AC coefficient value and raw data
            acCoefficientValues.push_back(ac.value);
            
            // Store raw entropy data for this coefficient
            size_t acDataSize = ac.byteEnd - ac.byteStart + 1;
            acCoefficientRawData.insert(acCoefficientRawData.end(), 
                                      entropyData + (ac.byteStart - baseOffset), 
                                      entropyData + (ac.byteStart - baseOffset) + acDataSize);
        }
        
        blockCount++;
    }
    
    return true;
}

// Split AC coefficients exactly using proper JPEG parsing
void splitACCoefficientsExact(const char* buffer, size_t size) {
    if (size == 0) return;
    
    // Clear previous data
    exactCoefficients.clear();
    criticalData.clear();
    acCoefficientValues.clear();
    acCoefficientRawData.clear();
    originalSize = size;
    
    const uint8_t* data = reinterpret_cast<const uint8_t*>(buffer);
    size_t pos = 0;
    
    // Parse JPEG markers and build Huffman tables
    std::map<uint8_t, HuffmanTable> huffmanTables;
    
    while (pos + 4 < size) {
        if (data[pos] != 0xFF) {
            return; // Invalid JPEG
        }
        
        uint8_t marker = data[pos + 1];
        pos += 2;
        
        if (marker == EOI_MARKER) {
            // End of image
            criticalData.insert(criticalData.end(), data + pos - 2, data + pos);
            break;
        }
        
        if (marker == SOS_MARKER) {
            // Start of scan - decode coefficients
            if (pos + 2 > size) return;
            
            uint16_t sosLength = readJPEGUint16(data + pos);
            if (pos + sosLength > size) return;
            
            parseSOSAndCoefficients(data + pos, sosLength, pos, huffmanTables);
            break;
        }
        
        if (marker == DHT_MARKER) {
            // Define Huffman Table
            if (pos + 2 > size) return;
            
            uint16_t dhtLength = readJPEGUint16(data + pos);
            if (pos + dhtLength > size) return;
            
            // Copy DHT to critical data
            criticalData.insert(criticalData.end(), data + pos - 2, data + pos + dhtLength);
            
            // Parse Huffman table
            parseDHT(data + pos + 2, dhtLength - 2, huffmanTables);
            pos += dhtLength;
            continue;
        }
        
        // Other markers - copy to critical data
        if (pos + 2 > size) return;
        uint16_t segmentLength = readJPEGUint16(data + pos);
        if (pos + segmentLength > size) return;
        
        criticalData.insert(criticalData.end(), data + pos - 2, data + pos + segmentLength);
        pos += segmentLength;
    }
}

// Rebuild JPEG data with exact coefficient placement
std::vector<uint8_t> rebuildJPEGDataExact() {
    std::vector<uint8_t> result;
    result.reserve(originalSize);
    
    // Start with critical data
    result.insert(result.end(), criticalData.begin(), criticalData.end());
    
    // Insert AC coefficients at their exact positions
    size_t acIndex = 0;
    for (const auto& coeff : exactCoefficients) {
        if (!coeff.isDC) {
            // This is an AC coefficient - insert its raw data
            size_t coeffSize = coeff.byteEnd - coeff.byteStart + 1;
            if (acIndex + coeffSize <= acCoefficientRawData.size()) {
                result.insert(result.end(), 
                            acCoefficientRawData.begin() + acIndex,
                            acCoefficientRawData.begin() + acIndex + coeffSize);
                acIndex += coeffSize;
            }
        }
    }
    
    return result;
}

ResultCode JpegFileHandler::createMapping(const char* buffer, size_t size) {
    
    return ResultCode::SUCCESS;
}

ResultCode JpegFileHandler::readFile(const char* mappingPath, char* buffer, size_t size, off_t offset) {
    // Rebuild the original JPEG data with exact coefficient placement
    std::vector<uint8_t> rebuiltData = rebuildJPEGDataExact();
    
    // Copy the requested portion
    if (offset + size <= rebuiltData.size()) {
        std::memcpy(buffer, rebuiltData.data() + offset, size);
        return ResultCode::SUCCESS;
    } else {
        return ResultCode::FAILURE;
    }
}

ResultCode JpegFileHandler::writeFile(const char* mappingPath, const char* buffer, size_t size, off_t offset) {
    // For write operations, re-parse the entire file
    if (offset == 0) {
        // Full file write
        splitACCoefficientsExact(buffer, size);
    } else {
        // Partial write - rebuild current data first
        std::vector<uint8_t> currentData = rebuildJPEGDataExact();
        
        // Ensure we have enough space
        if (offset + size > currentData.size()) {
            currentData.resize(offset + size);
        }
        
        // Apply the write
        std::memcpy(currentData.data() + offset, buffer, size);
        
        // Re-parse the data
        splitACCoefficientsExact(reinterpret_cast<const char*>(currentData.data()), currentData.size());
    }
    
    return ResultCode::SUCCESS;
}
