#include "BmpFile.h"
#include <cstring>
#include <cstdint>

ResultCode BmpFileHandler::createMapping(const char* buffer, size_t size) {
    if (size == 0) {
        return ResultCode::SUCCESS; // Nothing to map
    }
    if (size < 54) return ResultCode::FAILURE; // Minimum BMP size

    size_t origOffset = 0;
    size_t critOffset = 0;
    size_t noncritOffset = 0;

    // Check BMP signature
    if (buffer[0] != 'B' || buffer[1] != 'M') {
        return ResultCode::FAILURE;
    }

    // === Header ===
    // File header (14 bytes)
    addToFileMap(origOffset, origOffset + 13, critOffset, critOffset + 13, CriticalType::CRITICAL_DATA);
    origOffset += 14;
    critOffset += 14;

    // DIB header (assume BITMAPINFOHEADER, 40 bytes)
    addToFileMap(origOffset, origOffset + 39, critOffset, critOffset + 39, CriticalType::CRITICAL_DATA);
    critOffset += 40;

    // Extract image info
    const uint32_t pixelDataOffset = *reinterpret_cast<const uint32_t*>(buffer + 10);
    const int32_t width = *reinterpret_cast<const int32_t*>(buffer + 18);
    const int32_t height = *reinterpret_cast<const int32_t*>(buffer + 22);
    const uint16_t bitsPerPixel = *reinterpret_cast<const uint16_t*>(buffer + 28);

    if (bitsPerPixel != 24 || width <= 0 || height == 0 || pixelDataOffset > size) {
        return ResultCode::FAILURE; // Only 24-bit BMPs supported
    }

    // Color table or gap between headers and pixel data (mark critical)
    if (origOffset < pixelDataOffset) {
        size_t gap = pixelDataOffset - origOffset;
        addToFileMap(origOffset, origOffset + gap - 1, critOffset, critOffset + gap - 1, CriticalType::CRITICAL_DATA);
        origOffset += gap;
        critOffset += gap;
    }

    // === Pixel data ===
    const size_t bytesPerPixel = bitsPerPixel / 8;
    const size_t rowSizeUnpadded = width * bytesPerPixel;
    const size_t rowSizePadded = ((rowSizeUnpadded + 3) / 4) * 4;
    const size_t rowPadding = rowSizePadded - rowSizeUnpadded;
    const size_t absHeight = static_cast<size_t>(height < 0 ? -height : height);

    for (size_t row = 0; row < absHeight; ++row) {
        size_t rowStart = pixelDataOffset + row * rowSizePadded;
        size_t pixelStart = rowStart;
        size_t paddingStart = pixelStart + rowSizeUnpadded;

        // Bounds check
        if (paddingStart > size || paddingStart + rowPadding > size) {
            return ResultCode::FAILURE;
        }

        // Non-critical: pixel color data
        addToFileMap(pixelStart, paddingStart - 1, noncritOffset, noncritOffset + rowSizeUnpadded - 1, CriticalType::NON_CRITICAL_DATA);
        noncritOffset += rowSizeUnpadded;

        // Critical: padding bytes
        if (rowPadding > 0) {
            addToFileMap(paddingStart, paddingStart + rowPadding - 1, critOffset, critOffset + rowPadding - 1, CriticalType::CRITICAL_DATA);
            critOffset += rowPadding;
        }
    }

    return ResultCode::SUCCESS;
}
