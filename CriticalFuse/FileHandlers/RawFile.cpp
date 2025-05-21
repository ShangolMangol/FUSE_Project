#include "RawFile.h"
#include <iostream>
#include <cstring>

ResultCode RawFileHandler::createMapping(const char* buffer, size_t size) {
    if (!buffer || size == 0) {
        return ResultCode::FAILURE;
    }

    // For RAW files, we'll classify:
    // 1. First 1024 bytes as critical (header and metadata)
    // 2. Rest as non-critical (pixel data)
    
    const int HEADER_SIZE = 1024;  // Typical RAW header size
    
    // Add critical header section
    if (size > 0) {
        int headerEnd = std::min(HEADER_SIZE, static_cast<int>(size) - 1);
        addToFileMap(0, headerEnd, 0, headerEnd, CriticalType::CRITICAL_DATA);
    }
    
    // Add non-critical pixel data section
    if (size > HEADER_SIZE) {
        int pixelStart = HEADER_SIZE;
        int pixelEnd = static_cast<int>(size) - 1;
        addToFileMap(pixelStart, pixelEnd, 0, pixelEnd - pixelStart, CriticalType::NON_CRITICAL_DATA);
    }
    
    return ResultCode::SUCCESS;
}