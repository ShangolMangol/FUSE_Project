#include "DngFile.h"
#include <iostream>
#include <cstring>
#include <algorithm>

// Constants
const int TIFF_HEADER_SIZE = 8;
const int IFD_ENTRY_SIZE = 12;

// DNG-specific tag IDs
const uint16_t DNG_COLOR_MATRIX = 0xC621;
const uint16_t DNG_CAMERA_CALIBRATION = 0xC623;
const uint16_t DNG_AS_SHOT_NEUTRAL = 0xC628;
const uint16_t DNG_MAKER_NOTE = 0xC634;

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
    return tag == DNG_COLOR_MATRIX ||
           tag == DNG_CAMERA_CALIBRATION ||
           tag == DNG_AS_SHOT_NEUTRAL ||
           tag == DNG_MAKER_NOTE;
}

ResultCode DngFileHandler::createMapping(const char* buffer, size_t size) {
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

    // 4. Map TIFF header
    addToFileMap(0, TIFF_HEADER_SIZE - 1, 0, TIFF_HEADER_SIZE - 1, CriticalType::CRITICAL_DATA);

    // 5. Read number of IFD entries
    uint16_t entryCount = read16(buffer + ifdOffset, endian);
    size_t ifdSize = 2 + entryCount * IFD_ENTRY_SIZE + 4; // includes nextIFD offset
    if (ifdOffset + ifdSize > size) {
        std::cerr << "IFD size exceeds file size" << std::endl;
        return ResultCode::FAILURE;
    }

    // Map IFD as critical
    addToFileMap(ifdOffset, ifdOffset + ifdSize - 1,
                 TIFF_HEADER_SIZE, TIFF_HEADER_SIZE + ifdSize - 1,
                 CriticalType::CRITICAL_DATA);

    // 6. Parse IFD entries
    std::vector<std::pair<uint32_t, uint32_t>> imageBlocks;
    std::vector<std::pair<uint32_t, uint32_t>> metadataBlocks;

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

        // Handle DNG metadata tags
        if (isDngMetadataTag(tag)) {
            uint32_t dataSize = count * (type == 3 ? 2 : 4); // Approximate size
            if (valueOffset + dataSize <= size) {
                metadataBlocks.emplace_back(valueOffset, dataSize);
            }
        }
        // Handle image data tags
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
    }

    // 7. Map metadata blocks as critical
    size_t mappedOffset = TIFF_HEADER_SIZE + ifdSize;
    for (const auto& [offset, length] : metadataBlocks) {
        if (offset < size && length > 0 && offset + length <= size) {
            addToFileMap(offset, offset + length - 1,
                        mappedOffset, mappedOffset + length - 1,
                        CriticalType::CRITICAL_DATA);
            mappedOffset += length;
        }
    }

    // 8. Map image data blocks as non-critical
    mappedOffset = 0;
    for (const auto& [offset, length] : imageBlocks) {
        if (offset < size && length > 0 && offset + length <= size) {
            addToFileMap(offset, offset + length - 1,
                        mappedOffset, mappedOffset + length - 1,
                        CriticalType::NON_CRITICAL_DATA);
            mappedOffset += length;
        }
    }

    return ResultCode::SUCCESS;
}
