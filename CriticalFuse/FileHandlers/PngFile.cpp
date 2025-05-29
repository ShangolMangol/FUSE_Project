#include "PngFile.h"
#include <cstring>
#include <cstdint>  // for uint32_t

ResultCode PngFileHandler::createMapping(const char* buffer, size_t size) {
    if (size == 0) {
        return ResultCode::SUCCESS;
    }

    // PNG signature (8 bytes)
    const unsigned char pngSignature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (size < 8 || memcmp(buffer, pngSignature, 8) != 0) {
        return ResultCode::FAILURE; // Not a valid PNG file
    }

    size_t origOffset = 0;
    size_t critOffset = 0;
    size_t noncritOffset = 0;

    // Signature is critical
    addToFileMap(origOffset, origOffset + 7, critOffset, critOffset + 7, CriticalType::CRITICAL_DATA);
    origOffset += 8;
    critOffset += 8;

    while (origOffset + 8 <= size) {
        // Read chunk length (big endian)
        uint32_t chunkLength = (uint8_t(buffer[origOffset]) << 24) |
                               (uint8_t(buffer[origOffset + 1]) << 16) |
                               (uint8_t(buffer[origOffset + 2]) << 8) |
                               (uint8_t(buffer[origOffset + 3]));

        // Ensure chunk header + data + CRC fits in file
        if (origOffset + 12ULL + chunkLength > size) {
            break; // Truncated or malformed
        }

        // Read chunk type (4 chars)
        char chunkType[5] = {0};
        memcpy(chunkType, &buffer[origOffset + 4], 4);
        bool isCritical = (strcmp(chunkType, "IHDR") == 0 ||
                           strcmp(chunkType, "PLTE") == 0 ||
                           strcmp(chunkType, "IDAT") == 0 ||
                           strcmp(chunkType, "IEND") == 0);
        bool isIDAT = strcmp(chunkType, "IDAT") == 0;

        // Chunk header: 4 bytes length + 4 bytes type
        addToFileMap(origOffset, origOffset + 7, critOffset, critOffset + 7, CriticalType::CRITICAL_DATA);
        origOffset += 8;
        critOffset += 8;

        // Chunk data
        if (chunkLength > 0) {
            if (isIDAT) {
                // Non-critical pixel data
                addToFileMap(origOffset, origOffset + chunkLength - 1, noncritOffset, noncritOffset + chunkLength - 1, CriticalType::NON_CRITICAL_DATA);
                origOffset += chunkLength;
                noncritOffset += chunkLength;
            } else {
                CriticalType dataType = isCritical ? CriticalType::CRITICAL_DATA : CriticalType::NON_CRITICAL_DATA;
                size_t mappedStart = (dataType == CriticalType::CRITICAL_DATA) ? critOffset : noncritOffset;
                addToFileMap(origOffset, origOffset + chunkLength - 1,
                             mappedStart, mappedStart + chunkLength - 1, dataType);
                origOffset += chunkLength;
                if (dataType == CriticalType::CRITICAL_DATA)
                    critOffset += chunkLength;
                else
                    noncritOffset += chunkLength;
            }
        }

        // CRC (4 bytes): always critical
        addToFileMap(origOffset, origOffset + 3, critOffset, critOffset + 3, CriticalType::CRITICAL_DATA);
        origOffset += 4;
        critOffset += 4;
    }

    return ResultCode::SUCCESS;
}
