#include "TextFile.h"


ResultCode TextFileHandler::createMapping(const char* buffer, size_t size) {
    // For simplicity, let's make the critical data 5 bytes, then non-critical 5 bytes, and so on
    // This is just an example, you can implement your own logic
    // to determine critical and non-critical data
    size_t i = 0;
    while (i < size) {
        // Critical data
        int origStart = i;
        int origEnd = static_cast<int>(std::min(i + 4, size - 1));
        int mappedStart = i;
        int mappedEnd = origEnd;
        addToFileMap(origStart, origEnd, mappedStart, mappedEnd, CriticalType::CRITICAL_DATA);
        i += 5;

        // Non-critical data
        if (i < size) {
            origStart = i;
            origEnd = static_cast<int>(std::min(i + 4, size - 1));
            mappedStart = i;
            mappedEnd = origEnd;
            addToFileMap(origStart, origEnd, mappedStart, mappedEnd, CriticalType::NON_CRITICAL_DATA);
            i += 5;
        }
    }
    return ResultCode::SUCCESS;
}


