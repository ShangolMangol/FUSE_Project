#include "JpegFile.h"
#include <cstdint>
#include <cstring>

ResultCode JpegFileHandler::createMapping(const char* buffer, size_t size) {
    if (size == 0) {
        return ResultCode::SUCCESS; // Nothing to map
    }
    
    if (size < 2 || static_cast<uint8_t>(buffer[0]) != 0xFF || static_cast<uint8_t>(buffer[1]) != 0xD8) {
        return ResultCode::FAILURE; // Not a valid JPEG
    }

    size_t origOffset = 0;
    size_t critOffset = 0;
    size_t noncritOffset = 0;

    // SOI marker (Start of Image)
    addToFileMap(origOffset, origOffset + 1, critOffset, critOffset + 1, CriticalType::CRITICAL_DATA);
    origOffset += 2;
    critOffset += 2;

    while (origOffset + 4 <= size) {
        if (static_cast<uint8_t>(buffer[origOffset]) != 0xFF) {
            return ResultCode::FAILURE; // Invalid marker
        }

        uint8_t marker = static_cast<uint8_t>(buffer[origOffset + 1]);
        origOffset += 2;

        // Standalone marker (no payload)
        if (marker == 0xD9) { // EOI (End of Image)
            addToFileMap(origOffset - 2, origOffset - 1, critOffset, critOffset + 1, CriticalType::CRITICAL_DATA);
            critOffset += 2;
            break;
        }

        // Handle Start of Scan marker (SOS): pixel data follows
        if (marker == 0xDA) {
            if (origOffset + 2 > size) return ResultCode::FAILURE;
            uint16_t sosLength = (static_cast<uint8_t>(buffer[origOffset]) << 8) | static_cast<uint8_t>(buffer[origOffset + 1]);

            // Map SOS marker segment as critical
            addToFileMap(origOffset - 2, origOffset + sosLength - 1, critOffset, critOffset + sosLength + 1, CriticalType::CRITICAL_DATA);
            critOffset += sosLength + 2;
            origOffset += sosLength;

            // Scan for end of pixel data (next 0xFF marker)
            size_t scanStart = origOffset;
            while (origOffset + 1 < size) {
                if (static_cast<uint8_t>(buffer[origOffset]) == 0xFF && static_cast<uint8_t>(buffer[origOffset + 1]) != 0x00) {
                    break;
                }
                origOffset++;
            }

            // Map pixel data as NON_CRITICAL
            size_t scanLength = origOffset - scanStart;
            if (scanLength > 0) {
                addToFileMap(scanStart, origOffset - 1, noncritOffset, noncritOffset + scanLength - 1, CriticalType::NON_CRITICAL_DATA);
                noncritOffset += scanLength;
            }

            continue;
        }

        // Other segments with length field
        if (origOffset + 2 > size) return ResultCode::FAILURE;
        uint16_t segmentLength = (static_cast<uint8_t>(buffer[origOffset]) << 8) | static_cast<uint8_t>(buffer[origOffset + 1]);

        // Ensure full segment fits in buffer
        if (origOffset + segmentLength > size) {
            return ResultCode::FAILURE;
        }

        // Map entire segment as critical
        addToFileMap(origOffset - 2, origOffset + segmentLength - 1, critOffset, critOffset + segmentLength + 1, CriticalType::CRITICAL_DATA);
        critOffset += segmentLength + 2;
        origOffset += segmentLength;
    }

    return ResultCode::SUCCESS;
}
