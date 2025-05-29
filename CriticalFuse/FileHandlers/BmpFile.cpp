#include "BmpFile.h"
#include <cstring>
#include <cstdint>  // for uint32_t

ResultCode BmpFileHandler::createMapping(const char* buffer, size_t size) {
    if (size == 0) {
        return ResultCode::SUCCESS; // Nothing to map
    }
    
    if (size < 54) { // Minimum BMP size (14-byte header + 40-byte DIB header)
        return ResultCode::FAILURE;
    }

    size_t origOffset = 0;
    size_t critOffset = 0;
    size_t noncritOffset = 0;

    // BMP signature "BM" at offset 0 (2 bytes)
    if (buffer[0] != 'B' || buffer[1] != 'M') {
        return ResultCode::FAILURE;
    }

    // File header: 14 bytes (critical)
    addToFileMap(origOffset, origOffset + 13, critOffset, critOffset + 13, CriticalType::CRITICAL_DATA);
    origOffset += 14;
    critOffset += 14;

    // DIB header (BITMAPINFOHEADER): typically 40 bytes (critical)
    addToFileMap(origOffset, origOffset + 39, critOffset, critOffset + 39, CriticalType::CRITICAL_DATA);
    origOffset += 40;
    critOffset += 40;

    // Offset to pixel data is stored at bytes 10–13 (little-endian)
    uint32_t pixelDataOffset = *(const uint32_t*)(buffer + 10);
    if (pixelDataOffset > size) {
        return ResultCode::FAILURE;
    }

    // If there’s any padding or color table between headers and pixels, mark it critical too
    if (origOffset < pixelDataOffset) {
        size_t gap = pixelDataOffset - origOffset;
        addToFileMap(origOffset, origOffset + gap - 1, critOffset, critOffset + gap - 1, CriticalType::CRITICAL_DATA);
        origOffset += gap;
        critOffset += gap;
    }

    // Pixel data: from pixelDataOffset to end of file (non-critical)
    if (pixelDataOffset < size) {
        size_t pixelDataLength = size - pixelDataOffset;
        addToFileMap(origOffset, size - 1, noncritOffset, noncritOffset + pixelDataLength - 1, CriticalType::NON_CRITICAL_DATA);
        // no need to update origOffset after this
    }

    return ResultCode::SUCCESS;
}
