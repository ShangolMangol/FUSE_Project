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
static std::string currentMappingPath; // Store the current mapping path

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
    
    while (!reader.eof()) {
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
        
        //blockCount++;
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
    
    // Add some debugging
    std::cerr << "splitACCoefficientsExact: Processing " << size << " bytes" << std::endl;
    
    while (pos + 4 < size) {
        if (data[pos] != 0xFF) {
            std::cerr << "splitACCoefficientsExact: Invalid JPEG marker at position " << pos 
                      << " (expected 0xFF, got 0x" << std::hex << (int)data[pos] << std::dec << ")" << std::endl;
            // Instead of returning, try to find the next valid marker
            // Copy current data as critical and continue searching
            criticalData.insert(criticalData.end(), data + pos, data + pos + 1);
            pos++;
            continue;
        }
        
        uint8_t marker = data[pos + 1];
        pos += 2;
        
        std::cerr << "splitACCoefficientsExact: Found marker 0x" << std::hex << (int)marker << std::dec << std::endl;
        
        if (marker == EOI_MARKER) {
            // End of image
            criticalData.insert(criticalData.end(), data + pos - 2, data + pos);
            std::cerr << "splitACCoefficientsExact: Found EOI marker" << std::endl;
            break;
        }
        
        if (marker == SOS_MARKER) {
            // Start of scan - decode coefficients
            std::cerr << "splitACCoefficientsExact: Found SOS marker" << std::endl;
            if (pos + 2 > size) return;
            
            uint16_t sosLength = readJPEGUint16(data + pos);
            if (pos + sosLength > size) return;
            
            parseSOSAndCoefficients(data + pos, sosLength, pos, huffmanTables);
            break;
        }
        
        if (marker == DHT_MARKER) {
            // Define Huffman Table
            std::cerr << "splitACCoefficientsExact: Found DHT marker" << std::endl;
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
        std::cerr << "splitACCoefficientsExact: Found other marker 0x" << std::hex << (int)marker << std::dec << std::endl;
        if (pos + 2 > size) return;
        uint16_t segmentLength = readJPEGUint16(data + pos);
        if (pos + segmentLength > size) return;
        
        criticalData.insert(criticalData.end(), data + pos - 2, data + pos + segmentLength);
        pos += segmentLength;
    }
    
    std::cerr << "splitACCoefficientsExact: Finished. Critical data size: " << criticalData.size() 
              << ", AC coefficient data size: " << acCoefficientRawData.size() << std::endl;
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

// Rebuild JPEG data by parsing critical data and inserting AC coefficients
std::vector<uint8_t> rebuildJPEGFromCriticalData(const std::vector<uint8_t>& critData, const std::vector<uint8_t>& noncritData) {
    std::vector<uint8_t> result;
    result.reserve(critData.size() + noncritData.size());
    
    const uint8_t* data = critData.data();
    size_t size = critData.size();
    size_t pos = 0;
    size_t acIndex = 0;
    
    // Parse JPEG markers and rebuild with AC coefficients
    std::map<uint8_t, HuffmanTable> huffmanTables;
    
    while (pos + 4 < size) {
        if (data[pos] != 0xFF) {
            // Invalid JPEG, copy remaining data and return
            result.insert(result.end(), data + pos, data + size);
            break;
        }
        
        uint8_t marker = data[pos + 1];
        pos += 2;
        
        if (marker == EOI_MARKER) {
            // End of image - copy EOI marker
            result.insert(result.end(), data + pos - 2, data + pos);
            break;
        }
        
        if (marker == SOS_MARKER) {
            // Start of scan - copy SOS header and decode scan data
            if (pos + 2 > size) break;
            
            uint16_t sosLength = readJPEGUint16(data + pos);
            if (pos + sosLength > size) break;
            
            // Copy SOS header
            result.insert(result.end(), data + pos - 2, data + pos + sosLength);
            pos += sosLength;
            
            // Now we need to decode the scan data to find where to insert AC coefficients
            // The scan data after SOS header contains DC and AC coefficients
            // We need to parse this to separate DC (critical) from AC (non-critical)
            
            // Find the end of scan data (before EOI marker)
            size_t scanStart = pos;
            size_t scanEnd = size;
            
            // Look for EOI marker or next marker
            for (size_t i = pos; i < size - 1; i++) {
                if (data[i] == 0xFF && data[i + 1] != 0x00) {
                    scanEnd = i;
                    break;
                }
            }
            
            // For now, we'll use a simple approach: assume first 1/8 of scan data is DC coefficients
            // and the rest are AC coefficients
            size_t scanSize = scanEnd - scanStart;
            if (scanSize > 0) {
                size_t dcSize = scanSize / 8; // Assume 1/8 is DC coefficients
                if (dcSize == 0) dcSize = 1;
                
                // Copy DC coefficients (first part of scan data)
                result.insert(result.end(), data + scanStart, data + scanStart + dcSize);
                
                // Insert AC coefficients from noncrit file
                if (acIndex < noncritData.size()) {
                    result.insert(result.end(), noncritData.begin() + acIndex, noncritData.end());
                    acIndex = noncritData.size(); // Mark as consumed
                }
            }
            
            // Copy any remaining data (EOI marker, etc.)
            if (scanEnd < size) {
                result.insert(result.end(), data + scanEnd, data + size);
            }
            break;
        }
        
        if (marker == DHT_MARKER) {
            // Define Huffman Table - copy as is
            if (pos + 2 > size) break;
            
            uint16_t dhtLength = readJPEGUint16(data + pos);
            if (pos + dhtLength > size) break;
            
            result.insert(result.end(), data + pos - 2, data + pos + dhtLength);
            pos += dhtLength;
            continue;
        }
        
        // Other markers - copy as is
        if (pos + 2 > size) break;
        uint16_t segmentLength = readJPEGUint16(data + pos);
        if (pos + segmentLength > size) break;
        
        result.insert(result.end(), data + pos - 2, data + pos + segmentLength);
        pos += segmentLength;
    }
    
    return result;
}

ResultCode JpegFileHandler::createMapping(const char* buffer, size_t size) {
    return ResultCode::SUCCESS;
}

ResultCode JpegFileHandler::readFile(const char* mappingPath, char* buffer, size_t size, off_t offset) {
    // Remove .mapping suffix from the path
    std::string basePath(mappingPath);
    const std::string mappingSuffix = ".mapping";
    if (basePath.size() > mappingSuffix.size() && 
        basePath.substr(basePath.size() - mappingSuffix.size()) == mappingSuffix) {
        basePath = basePath.substr(0, basePath.size() - mappingSuffix.size());
    }
    
    // Read critical data from .crit file
    std::string critPath = basePath + ".crit";
    std::ifstream critFile(critPath, std::ios::binary);
    if (!critFile.is_open()) {
        std::cerr << "Failed to open critical file: " << critPath << std::endl;
        return ResultCode::FAILURE;
    }
    
    std::vector<uint8_t> critData((std::istreambuf_iterator<char>(critFile)), {});
    critFile.close();
    
    // Read AC coefficient data from .noncrit file
    std::string noncritPath = basePath + ".noncrit";
    std::ifstream noncritFile(noncritPath, std::ios::binary);
    if (!noncritFile.is_open()) {
        std::cerr << "Failed to open non-critical file: " << noncritPath << std::endl;
        return ResultCode::FAILURE;
    }
    
    std::vector<uint8_t> noncritData((std::istreambuf_iterator<char>(noncritFile)), {});
    noncritFile.close();
    
    // Rebuild the original JPEG data by parsing critical data and inserting AC coefficients
    std::vector<uint8_t> result = rebuildJPEGFromCriticalData(critData, noncritData);
    
    // Copy the requested portion
    if (offset + size <= result.size()) {
        std::memcpy(buffer, result.data() + offset, size);
        return ResultCode::SUCCESS;
    } else {
        std::cerr << "Requested range exceeds file size" << std::endl;
        return ResultCode::FAILURE;
    }
}

ResultCode JpegFileHandler::writeFile(const char* mappingPath, const char* buffer, size_t size, off_t offset) {
    // Remove .mapping suffix from the path
    std::string basePath(mappingPath);
    const std::string mappingSuffix = ".mapping";
    if (basePath.size() > mappingSuffix.size() && 
        basePath.substr(basePath.size() - mappingSuffix.size()) == mappingSuffix) {
        basePath = basePath.substr(0, basePath.size() - mappingSuffix.size());
    }
    
    std::cerr << "writeFile: Processing " << size << " bytes, offset " << offset << std::endl;
    std::cerr << "writeFile: Base path: " << basePath << std::endl;
    
    // For write operations, re-parse the entire file
    if (offset == 0) {
        // Full file write
        std::cerr << "writeFile: Full file write, calling splitACCoefficientsExact" << std::endl;
        splitACCoefficientsExact(buffer, size);
        
        std::cerr << "writeFile: After parsing - critical data size: " << criticalData.size() 
                  << ", AC coefficient data size: " << acCoefficientRawData.size() << std::endl;
        
        // Write updated critical data to .crit file
        std::string critPath = basePath + ".crit";
        std::ofstream critFile(critPath, std::ios::binary);
        if (critFile.is_open()) {
            critFile.write(reinterpret_cast<const char*>(criticalData.data()), criticalData.size());
            critFile.close();
            std::cerr << "writeFile: Wrote " << criticalData.size() << " bytes to " << critPath << std::endl;
        } else {
            std::cerr << "writeFile: Failed to open " << critPath << " for writing" << std::endl;
        }
        
        // Write updated AC coefficient data to .noncrit file
        std::string noncritPath = basePath + ".noncrit";
        std::ofstream noncritFile(noncritPath, std::ios::binary);
        if (noncritFile.is_open()) {
            noncritFile.write(reinterpret_cast<const char*>(acCoefficientRawData.data()), acCoefficientRawData.size());
            noncritFile.close();
            std::cerr << "writeFile: Wrote " << acCoefficientRawData.size() << " bytes to " << noncritPath << std::endl;
        } else {
            std::cerr << "writeFile: Failed to open " << noncritPath << " for writing" << std::endl;
        }
    } else {
        // Partial write - this is more complex and would require rebuilding the entire file
        // For now, we'll re-parse the entire file
        std::vector<uint8_t> currentData;
        
        // Read existing data
        std::string critPath = basePath + ".crit";
        std::ifstream critFile(critPath, std::ios::binary);
        if (critFile.is_open()) {
            std::vector<uint8_t> critData((std::istreambuf_iterator<char>(critFile)), {});
            currentData.insert(currentData.end(), critData.begin(), critData.end());
            critFile.close();
        }
        
        std::string noncritPath = basePath + ".noncrit";
        std::ifstream noncritFile(noncritPath, std::ios::binary);
        if (noncritFile.is_open()) {
            std::vector<uint8_t> noncritData((std::istreambuf_iterator<char>(noncritFile)), {});
            currentData.insert(currentData.end(), noncritData.begin(), noncritData.end());
            noncritFile.close();
        }
        
        // Ensure we have enough space
        if (offset + size > currentData.size()) {
            currentData.resize(offset + size);
        }
        
        // Apply the write
        std::memcpy(currentData.data() + offset, buffer, size);
        
        // Re-parse the data
        splitACCoefficientsExact(reinterpret_cast<const char*>(currentData.data()), currentData.size());
        
        // Write updated files
        std::ofstream critFileOut(critPath, std::ios::binary);
        if (critFileOut.is_open()) {
            critFileOut.write(reinterpret_cast<const char*>(criticalData.data()), criticalData.size());
            critFileOut.close();
        }
        
        std::ofstream noncritFileOut(noncritPath, std::ios::binary);
        if (noncritFileOut.is_open()) {
            noncritFileOut.write(reinterpret_cast<const char*>(acCoefficientRawData.data()), acCoefficientRawData.size());
            noncritFileOut.close();
        }
    }
    
    return ResultCode::SUCCESS;
}
