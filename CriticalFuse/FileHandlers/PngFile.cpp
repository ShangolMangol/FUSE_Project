#include "PngFile.h"
#include <cstring>
#include <cstdint>

ResultCode PngFileHandler::createMapping(const char* buffer1, size_t size) {
    if (size == 0) {
        return ResultCode::SUCCESS;
    }

    const unsigned char* buffer = reinterpret_cast<const unsigned char*>(buffer1);

    // PNG signature is 8 bytes
    const unsigned char pngSignature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (size < 8 || memcmp(buffer, pngSignature, 8) != 0) {
        return ResultCode::FAILURE; // Not a valid PNG file
    }

    size_t origOffset = 0;
    size_t criticalOffset = 0;
    size_t nonCriticalOffset = 0;

    // Add PNG signature as critical data
    addToFileMap(origOffset, origOffset + 7, criticalOffset, criticalOffset + 7, CriticalType::CRITICAL_DATA);
    origOffset += 8;
    criticalOffset += 8;

    while (origOffset + 8 <= size) {
        // Check enough bytes for header
        uint32_t chunkLength = (buffer[origOffset] << 24) |
                               (buffer[origOffset + 1] << 16) |
                               (buffer[origOffset + 2] << 8) |
                               (buffer[origOffset + 3]);

        if (origOffset + 12ULL + chunkLength > size) {
            return ResultCode::FAILURE;
        }

        const char* chunkTypePtr = reinterpret_cast<const char*>(&buffer[origOffset + 4]);
        bool isCritical = (strncmp(chunkTypePtr, "IHDR", 4) == 0 ||
                           strncmp(chunkTypePtr, "PLTE", 4) == 0 ||
                           strncmp(chunkTypePtr, "IDAT", 4) == 0 ||
                           strncmp(chunkTypePtr, "IEND", 4) == 0);

        CriticalType dataType = isCritical ? CriticalType::CRITICAL_DATA : CriticalType::NON_CRITICAL_DATA;
        size_t* mappedOffsetPtr = isCritical ? &criticalOffset : &nonCriticalOffset;

        // Add chunk header (length + type) to critical file (these are always critical!)
        addToFileMap(origOffset, origOffset + 7, criticalOffset, criticalOffset + 7, CriticalType::CRITICAL_DATA);
        origOffset += 8;
        criticalOffset += 8;

        // Add chunk data
        if (chunkLength > 0) {
            addToFileMap(origOffset, origOffset + chunkLength - 1,
                         *mappedOffsetPtr, *mappedOffsetPtr + chunkLength - 1, dataType);
            origOffset += chunkLength;
            *mappedOffsetPtr += chunkLength;
        }

        // Add CRC
        addToFileMap(origOffset, origOffset + 3,
                     *mappedOffsetPtr, *mappedOffsetPtr + 3, dataType);
        origOffset += 4;
        *mappedOffsetPtr += 4;
    }

    return ResultCode::SUCCESS;
}

