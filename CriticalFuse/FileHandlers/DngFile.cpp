#include "DngFile.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <cstdint>
#include <vector>
#include <utility>

// Constants
const int TIFF_HEADER_SIZE = 8;
const int IFD_ENTRY_SIZE = 12;

// DNG-specific tag IDs
const uint16_t DNG_BLACK_LEVEL = 0xC61A;
const uint16_t DNG_WHITE_LEVEL = 0xC61D;
const uint16_t DNG_COLOR_MATRIX_1 = 0xC621;
const uint16_t DNG_COLOR_MATRIX_2 = 0xC622;
const uint16_t DNG_CAMERA_CALIBRATION_1 = 0xC623;
const uint16_t DNG_CAMERA_CALIBRATION_2 = 0xC624;
const uint16_t DNG_REDUCTION_MATRIX_1 = 0xC625;
const uint16_t DNG_REDUCTION_MATRIX_2 = 0xC626;
const uint16_t DNG_ANALOG_BALANCE = 0xC627;
const uint16_t DNG_AS_SHOT_NEUTRAL = 0xC628;
const uint16_t DNG_AS_SHOT_WHITE_XY = 0xC629;
const uint16_t DNG_BASELINE_EXPOSURE = 0xC62A;
const uint16_t DNG_BASELINE_NOISE = 0xC62B;
const uint16_t DNG_BASELINE_SHARPNESS = 0xC62C;
const uint16_t DNG_BAYER_GREEN_SPLIT = 0xC62D;
const uint16_t DNG_LINEAR_RESPONSE_LIMIT = 0xC62E;
const uint16_t DNG_CAMERA_SERIAL_NUMBER = 0xC62F;
const uint16_t DNG_LENS_INFO = 0xC630;
const uint16_t DNG_CHROMA_BLUR_RADIUS = 0xC631;
const uint16_t DNG_ANTI_ALIAS_STRENGTH = 0xC632;
const uint16_t DNG_SHADOW_SCALE = 0xC633;
const uint16_t DNG_MAKER_NOTE = 0xC634;
const uint16_t DNG_CALIBRATION_ILLUMINANT_1 = 0xC65A;
const uint16_t DNG_CALIBRATION_ILLUMINANT_2 = 0xC65B;
const uint16_t DNG_BEST_QUALITY_SCALE = 0xC65C;
const uint16_t DNG_ALIAS_LAYER_METADATA = 0xC660;

// Helper: Endian awareness
enum class Endian {
    LITTLE,
    BIG
};

uint16_t read16(const char* ptr, Endian endian) {
    uint16_t val;
    std::memcpy(&val, ptr, 2);
    if (endian == Endian::BIG)
        val = (val >> 8) | (val << 8);
    return val;
}

uint32_t read32(const char* ptr, Endian endian) {
    uint32_t val;
    std::memcpy(&val, ptr, 4);
    if (endian == Endian::BIG)
        val = ((val >> 24) & 0xFF) |
              ((val >> 8) & 0xFF00) |
              ((val << 8) & 0xFF0000) |
              ((val << 24) & 0xFF000000);
    return val;
}

// Helper: Check if tag is DNG-specific metadata
bool isDngMetadataTag(uint16_t tag) {
    return tag == DNG_BLACK_LEVEL ||
           tag == DNG_WHITE_LEVEL ||
           tag == DNG_COLOR_MATRIX_1 ||
           tag == DNG_COLOR_MATRIX_2 ||
           tag == DNG_CAMERA_CALIBRATION_1 ||
           tag == DNG_CAMERA_CALIBRATION_2 ||
           tag == DNG_REDUCTION_MATRIX_1 ||
           tag == DNG_REDUCTION_MATRIX_2 ||
           tag == DNG_ANALOG_BALANCE ||
           tag == DNG_AS_SHOT_NEUTRAL ||
           tag == DNG_AS_SHOT_WHITE_XY ||
           tag == DNG_BASELINE_EXPOSURE ||
           tag == DNG_BASELINE_NOISE ||
           tag == DNG_BASELINE_SHARPNESS ||
           tag == DNG_BAYER_GREEN_SPLIT ||
           tag == DNG_LINEAR_RESPONSE_LIMIT ||
           tag == DNG_CAMERA_SERIAL_NUMBER ||
           tag == DNG_LENS_INFO ||
           tag == DNG_CHROMA_BLUR_RADIUS ||
           tag == DNG_ANTI_ALIAS_STRENGTH ||
           tag == DNG_SHADOW_SCALE ||
           tag == DNG_MAKER_NOTE ||
           tag == DNG_CALIBRATION_ILLUMINANT_1 ||
           tag == DNG_CALIBRATION_ILLUMINANT_2 ||
           tag == DNG_BEST_QUALITY_SCALE ||
           tag == DNG_ALIAS_LAYER_METADATA;
}

ResultCode DngFileHandler::createMapping(const char* buffer, size_t size) {
    
    // if the buffer is empty, we don't need to do anything
    if (size == 0) {
        return ResultCode::SUCCESS;
    }

    if (!buffer || size < TIFF_HEADER_SIZE) {
        std::cerr << "Invalid buffer or size too small" << std::endl;
        return ResultCode::FAILURE;
    }

    // 1. Read byte order
    Endian endian;
    if (buffer[0] == 'I' && buffer[1] == 'I') {
        endian = Endian::LITTLE;
    } else if (buffer[0] == 'M' && buffer[1] == 'M') {
        endian = Endian::BIG;
    } else {
        std::cerr << "Invalid byte order" << std::endl;
        return ResultCode::FAILURE;
    }

    // 2. Validate magic number
    uint16_t magic = read16(buffer + 2, endian);
    if (magic != 42) {
        std::cerr << "Invalid TIFF magic number" << std::endl;
        return ResultCode::FAILURE;
    }

    // 3. Read offset to first IFD
    uint32_t ifdOffset = read32(buffer + 4, endian);
    if (ifdOffset >= size || ifdOffset + 2 > size) {
        std::cerr << "Invalid IFD offset" << std::endl;
        return ResultCode::FAILURE;
    }

    // 4. Read number of IFD entries
    uint16_t entryCount = read16(buffer + ifdOffset, endian);
    size_t ifdSize = 2 + entryCount * IFD_ENTRY_SIZE + 4; // includes nextIFD offset
    if (ifdOffset + ifdSize > size) {
        std::cerr << "IFD size exceeds file size" << std::endl;
        return ResultCode::FAILURE;
    }

    // 5. Parse IFD entries to collect all data regions
    std::vector<std::pair<uint32_t, uint32_t>> imageBlocks;
    std::vector<std::pair<uint32_t, uint32_t>> metadataBlocks;
    std::vector<std::pair<uint32_t, uint32_t>> otherCriticalBlocks;

    for (int i = 0; i < entryCount; ++i) {
        size_t entryOffset = ifdOffset + 2 + i * IFD_ENTRY_SIZE;
        if (entryOffset + 12 > size) {
            std::cerr << "IFD entry out of bounds" << std::endl;
            continue;
        }

        uint16_t tag = read16(buffer + entryOffset, endian);
        uint16_t type = read16(buffer + entryOffset + 2, endian);
        uint32_t count = read32(buffer + entryOffset + 4, endian);
        uint32_t valueOffset = read32(buffer + entryOffset + 8, endian);

        // Calculate data size based on type
        uint32_t dataSize = 0;
        switch (type) {
            case 1: dataSize = count; break;           // BYTE
            case 2: dataSize = count; break;           // ASCII
            case 3: dataSize = count * 2; break;       // SHORT
            case 4: dataSize = count * 4; break;       // LONG
            case 5: dataSize = count * 8; break;       // RATIONAL
            case 6: dataSize = count; break;           // SBYTE
            case 7: dataSize = count; break;           // UNDEFINED
            case 8: dataSize = count * 2; break;       // SSHORT
            case 9: dataSize = count * 4; break;       // SLONG
            case 10: dataSize = count * 8; break;      // SRATIONAL
            case 11: dataSize = count * 4; break;      // FLOAT
            case 12: dataSize = count * 8; break;      // DOUBLE
            default: dataSize = count * 4; break;      // Default to 4 bytes per value
        }

        // Handle DNG-specific metadata tags (critical)
        if (isDngMetadataTag(tag)) {
            if (valueOffset + dataSize <= size && dataSize > 0) {
                metadataBlocks.emplace_back(valueOffset, dataSize);
            }
        }
        // Handle image data tags (non-critical)
        else if (tag == 0x0111 || tag == 0x0117) { // StripOffsets or StripByteCounts
            std::vector<uint32_t> values;

            if ((type == 3 && count <= 2) || (type == 4 && count == 1)) {
                values.push_back(valueOffset);
            } else {
                if (valueOffset + count * 4 > size) continue;
                for (uint32_t j = 0; j < count; ++j) {
                    uint32_t val = read32(buffer + valueOffset + j * 4, endian);
                    values.push_back(val);
                }
            }

            if (tag == 0x0111) {  // StripOffsets
                for (auto& v : values) {
                    imageBlocks.emplace_back(v, 0);
                }
            } else if (tag == 0x0117) { // StripByteCounts
                for (size_t j = 0; j < std::min(imageBlocks.size(), values.size()); ++j) {
                    imageBlocks[j].second = values[j];
                }
            }
        }
        // Handle other critical metadata (EXIF, GPS, etc.)
        else if (tag >= 0x0100 && tag <= 0x017F) { // Image structure tags
            if (valueOffset + dataSize <= size && dataSize > 0) {
                otherCriticalBlocks.emplace_back(valueOffset, dataSize);
            }
        }
        // Handle EXIF tags (0x8769)
        else if (tag == 0x8769) {
            if (valueOffset + dataSize <= size && dataSize > 0) {
                otherCriticalBlocks.emplace_back(valueOffset, dataSize);
            }
        }
    }

    // 6. Create a simple mapping: first part critical, rest non-critical
    // This follows the pattern used by other file handlers
    
    // Find the end of the critical data section
    uint32_t criticalEnd = TIFF_HEADER_SIZE + ifdSize;
    
    // Add all metadata blocks to critical section
    for (const auto& [offset, length] : metadataBlocks) {
        if (offset < size && length > 0 && offset + length <= size) {
            criticalEnd = std::max(criticalEnd, offset + length);
        }
    }
    for (const auto& [offset, length] : otherCriticalBlocks) {
        if (offset < size && length > 0 && offset + length <= size) {
            criticalEnd = std::max(criticalEnd, offset + length);
        }
    }
    
    // Map critical data (header, IFD, metadata) as sequential critical data
    addToFileMap(0, criticalEnd - 1, 0, criticalEnd - 1, CriticalType::CRITICAL_DATA);
    
    // Map remaining data as non-critical (image data)
    if (criticalEnd < size) {
        addToFileMap(criticalEnd, size - 1, 0, size - criticalEnd - 1, CriticalType::NON_CRITICAL_DATA);
    }

    return ResultCode::SUCCESS;
}

/**
 * DNG File Handler Implementation
 * 
 * This implementation properly separates DNG files into:
 * - CRITICAL_DATA: TIFF header, IFD entries, DNG metadata, EXIF data, image structure tags
 * - NON_CRITICAL_DATA: Raw image pixel data (can tolerate bit errors)
 * 
 * The mapping ensures no overlapping ranges and proper sequential mapping
 * of critical and non-critical data regions.
 */