#include "BmpFile.h"
#include <cstring>
#include <cstdint>

ResultCode BmpFileHandler::createMapping(const char* buffer, size_t size) {
    if (size == 0) {
        return ResultCode::SUCCESS; // Nothing to map
    }
        size_t origOffset = 0;
    size_t critOffset = 0;
    size_t noncritOffset = 0;

    // Check BMP signature
    if (buffer[0] != 'B' || buffer[1] != 'M') {
        return ResultCode::FAILURE;
    }

    // File header (14 bytes) — critical
    addToFileMap(origOffset, origOffset + 13, critOffset, critOffset + 13, CriticalType::CRITICAL_DATA);
    origOffset += 14;
    critOffset += 14;

    // DIB header (assume BITMAPINFOHEADER — 40 bytes) — critical
    addToFileMap(origOffset, origOffset + 39, critOffset, critOffset + 39, CriticalType::CRITICAL_DATA);

    // Get image dimensions and pixel data offset
    int32_t width = *(const int32_t*)(buffer + 18);
    int32_t height = *(const int32_t*)(buffer + 22);
    uint16_t bitsPerPixel = *(const uint16_t*)(buffer + 28);
    uint32_t pixelDataOffset = *(const uint32_t*)(buffer + 10);
    origOffset += 40;
    critOffset += 40;

    if (pixelDataOffset > size || bitsPerPixel != 24 || width <= 0 || height == 0) {
        return ResultCode::FAILURE; // Only 24-bit BMP supported
    }

    // Critical area before pixel data (e.g., color table if any)
    if (origOffset < pixelDataOffset) {
        size_t gap = pixelDataOffset - origOffset;
        addToFileMap(origOffset, origOffset + gap - 1, critOffset, critOffset + gap - 1, CriticalType::CRITICAL_DATA);
        origOffset += gap;
        critOffset += gap;
    }

    // Row padding: each row in BMP is aligned to 4 bytes
    size_t rowSize = ((width * 3 + 3) / 4) * 4;
    size_t pixelSize = width * 3;
    size_t absHeight = height > 0 ? height : -height;

    for (size_t row = 0; row < absHeight; ++row) {
        if (origOffset + rowSize > size) return ResultCode::FAILURE;

        // Map pixel data (non-critical)
        addToFileMap(origOffset, origOffset + pixelSize - 1, noncritOffset, noncritOffset + pixelSize - 1, CriticalType::NON_CRITICAL_DATA);
        origOffset += pixelSize;
        noncritOffset += pixelSize;

        // Map padding (critical)
        size_t padding = rowSize - pixelSize;
        if (padding > 0) {
            addToFileMap(origOffset, origOffset + padding - 1, critOffset, critOffset + padding - 1, CriticalType::CRITICAL_DATA);
            origOffset += padding;
            critOffset += padding;
        }
    }

    return ResultCode::SUCCESS;
}
