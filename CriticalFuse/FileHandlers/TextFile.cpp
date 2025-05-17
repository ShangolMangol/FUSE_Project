#include "TextFile.h"


ResultCode TextFileHandler::createMapping(const char* buffer, size_t size) {
    // For simplicity, let's make the critical data 5 bytes, then non-critical 5 bytes, and so on
    // This is just an example, you can implement your own logic
    // to determine critical and non-critical data

    // reminder: addToFileMap(int origStart, int origEnd, int mappedStart, int mappedEnd, CriticalType type)
    
    size_t i = 0;
    int critOffset = 0;
    int nonCritOffset = 0;

    while (i < size) {
        // Critical data block (up to 5 bytes)
        if (i < size) {
            int origStart = static_cast<int>(i);
            int origEnd = static_cast<int>(std::min(i + 4, size - 1));
            int length = origEnd - origStart + 1;

            addToFileMap(origStart, origEnd,
                        critOffset, critOffset + length - 1,
                        CriticalType::CRITICAL_DATA);

            critOffset += length;
            i += 5;
        }

        // Non-critical data block (up to 5 bytes)
        if (i < size) {
            int origStart = static_cast<int>(i);
            int origEnd = static_cast<int>(std::min(i + 4, size - 1));
            int length = origEnd - origStart + 1;

            addToFileMap(origStart, origEnd,
                        nonCritOffset, nonCritOffset + length - 1,
                        CriticalType::NON_CRITICAL_DATA);

            nonCritOffset += length;
            i += 5;
        }
    }
    return ResultCode::SUCCESS;

}


