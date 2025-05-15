#include "AbstractFile.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>


std::map<Range, std::pair<Range, CriticalType>>& AbstractFileHandler::getFileMap() {
    return fileMap;
}

ResultCode AbstractFileHandler::setFileMap(const std::map<Range, std::pair<Range, CriticalType>>& newFileMap) {
    fileMap = newFileMap;
    return ResultCode::SUCCESS;
}


ResultCode AbstractFileHandler::addToFileMap(int origStart, int origEnd, int mappedStart, int mappedEnd, CriticalType type) {
    try {
        Range originalRange(origStart, origEnd);
        Range mappedRange(mappedStart, mappedEnd);
        this->fileMap[originalRange] = std::make_pair(mappedRange, type);
        return ResultCode::SUCCESS;
    } catch (const std::exception& e) {
        return ResultCode::FAILURE;
    }

}

ResultCode AbstractFileHandler::loadMapFromFile(const char* mappingPath) {
    std::ifstream inFile(mappingPath);
    if (!inFile.is_open()) {
        std::cerr << "Failed to open mapping file: " << mappingPath << std::endl;
        return ResultCode::FAILURE;
    }

    std::string line;

    while (std::getline(inFile, line)) {
        std::istringstream iss(line);
        std::string originalRangeStr, mappedRangeStr, typeStr;

        if (!(iss >> originalRangeStr >> mappedRangeStr >> typeStr)) {
            std::cerr << "Malformed line: " << line << std::endl;
            return ResultCode::FAILURE;
        }

        // Parse original range
        int origStart, origEnd;
        size_t dashPos = originalRangeStr.find('-');
        if (dashPos == std::string::npos) return ResultCode::FAILURE;
        origStart = std::stoi(originalRangeStr.substr(0, dashPos));
        origEnd = std::stoi(originalRangeStr.substr(dashPos + 1));

        // Parse mapped range
        int mappedStart, mappedEnd;
        dashPos = mappedRangeStr.find('-');
        if (dashPos == std::string::npos) return ResultCode::FAILURE;
        mappedStart = std::stoi(mappedRangeStr.substr(0, dashPos));
        mappedEnd = std::stoi(mappedRangeStr.substr(dashPos + 1));

        // Parse critical type
        CriticalType type;
        if (typeStr == "CRITICAL_DATA") {
            type = CriticalType::CRITICAL_DATA;
        } else if (typeStr == "NON_CRITICAL_DATA") {
            type = CriticalType::NON_CRITICAL_DATA;
        } else {
            std::cerr << "Unknown CriticalType: " << typeStr << std::endl;
            return ResultCode::FAILURE;
        }

        
        if(addToFileMap(origStart, origEnd, mappedStart, mappedEnd, type) != ResultCode::SUCCESS)
        {        
            std::cerr << "Failed to add line to file map: " << line << std::endl;
            return ResultCode::FAILURE;
        }
    }

    inFile.close();
    return ResultCode::SUCCESS;
}

ResultCode AbstractFileHandler::saveMapToFile(const char* mappingPath) {
    std::ofstream outFile(mappingPath);
    if (!outFile.is_open()) {
        std::cerr << "Failed to open file for writing: " << mappingPath << std::endl;
        return ResultCode::FAILURE;
    }

    for (const auto& entry : fileMap) {
        const Range& original_range = entry.first;
        const Range& mapped_range = entry.second.first;
        CriticalType type = entry.second.second;

        outFile << original_range.getStart() << '-' << original_range.getEnd() << ' '
                << mapped_range.getStart() << '-' << mapped_range.getEnd() << ' '
                << (type == CriticalType::CRITICAL_DATA ? "CRITICAL_DATA" : "NON_CRITICAL_DATA") << '\n';
    }

    outFile.close();
    return ResultCode::SUCCESS;
}

/**
 * 
 * THIS IS WRONG, IT NEEDS TO READ FROM THE CRITICAL AND NON-CRITICAL FILES
 * 
 */
ResultCode AbstractFileHandler::readFile(const char* mappingPath, char* buffer, size_t size, off_t offset) {
     std::memset(buffer, 0, size);  // zero-initialize the buffer

    int fd = open(mappingPath, O_RDONLY);
    if (fd < 0) {
        std::perror("Failed to open data file for reading");
        return ResultCode::FAILURE;
    }

    off_t readEnd = offset + size - 1;

    for (const auto& [originalRange, mappedPair] : fileMap) {
        const Range& mappedRange = mappedPair.first;

        // Skip if the requested range [offset, readEnd] does not intersect with originalRange
        if (readEnd < originalRange.getStart() || offset > originalRange.getEnd()) {
            continue;
        }

        // Compute overlap
        int overlapStart = std::max(static_cast<int>(offset), originalRange.getStart());
        int overlapEnd = std::min(static_cast<int>(readEnd), originalRange.getEnd());
        size_t bytesToRead = overlapEnd - overlapStart + 1;

        size_t bufferOffset = overlapStart - offset;
        off_t mappedOffset = mappedRange.getStart() + (overlapStart - originalRange.getStart());

        if (lseek(fd, mappedOffset, SEEK_SET) < 0) {
            std::perror("lseek failed");
            close(fd);
            return ResultCode::FAILURE;
        }

        ssize_t bytesRead = read(fd, buffer + bufferOffset, bytesToRead);
        if (bytesRead != static_cast<ssize_t>(bytesToRead)) {
            std::perror("read failed");
            close(fd);
            return ResultCode::FAILURE;
        }
    }

    close(fd);
    return ResultCode::SUCCESS;
}

ResultCode AbstractFileHandler::writeFile(const char* mappingPath, const char* buffer, size_t size, off_t offset) {

    // if mapping already exists, load mapping file, then merge it, write to it, and call analyzeCriticalAreas(mergedFileBuffer, totalSize)
    // else, createMapping(buffer, size);

    // after creating the mapping, save the mapping to the file
    // saveMapToFile(mappingPath);
}