#include "AbstractFile.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <fcntl.h>      // For open
#include <unistd.h>     // For close, read, lseek
#include <sys/types.h>  // For off_t, ssize_t




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

    std::ofstream outFile(mappingPath, std::ios::binary | std::ios::trunc);
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

ResultCode AbstractFileHandler::readFile(const char* mappingPath, char* buffer, size_t size, off_t offset) {
    // Load the mapping
    if (loadMapFromFile(mappingPath) != ResultCode::SUCCESS) {
        std::cerr << "Failed to load file map from: " << mappingPath << std::endl;
        return ResultCode::FAILURE;
    }

    std::memset(buffer, 0, size);  // zero-initialize output buffer

    // Derive base path by removing ".mapping" suffix
    std::string basePath(mappingPath);
    const std::string mappingSuffix = ".mapping";
    if (basePath.size() <= mappingSuffix.size() || basePath.substr(basePath.size() - mappingSuffix.size()) != mappingSuffix) {
        std::cerr << "Invalid mappingPath: missing .mapping suffix\n";
        return ResultCode::FAILURE;
    }
    basePath = basePath.substr(0, basePath.size() - mappingSuffix.size());

    // Create paths for critical and non-critical data
    std::string criticalPath = basePath + ".crit";
    std::string nonCriticalPath = basePath + ".noncrit";

    int fdCrit = open(criticalPath.c_str(), O_RDONLY);
    int fdNonCrit = open(nonCriticalPath.c_str(), O_RDONLY);

    if (fdCrit < 0 || fdNonCrit < 0) {
        std::perror("Failed to open critical or non-critical data file");
        if (fdCrit >= 0) close(fdCrit);
        if (fdNonCrit >= 0) close(fdNonCrit);
        return ResultCode::FAILURE;
    }

    off_t readEnd = offset + size - 1;
    bool readSuccess = true;

    for (const auto& [originalRange, mappedPair] : fileMap) {
        const Range& mappedRange = mappedPair.first;
        CriticalType type = mappedPair.second;

        // Skip if not overlapping with read range
        if (readEnd < originalRange.getStart() || offset > originalRange.getEnd()) {
            continue;
        }

        // Calculate overlap
        // we want to read [overlapStart, overlapEnd] from the orignal range 
        int overlapStart = std::max(static_cast<int>(offset), originalRange.getStart());
        int overlapEnd = std::min(static_cast<int>(readEnd), originalRange.getEnd());
        size_t bytesToRead = overlapEnd - overlapStart + 1;

        size_t bufferOffset = overlapStart - offset;
        off_t mappedOffset = mappedRange.getStart() + std::max(0, overlapStart - originalRange.getStart());

        int fd = (type == CriticalType::CRITICAL_DATA) ? fdCrit : fdNonCrit;

        if (lseek(fd, mappedOffset, SEEK_SET) < 0) {
            std::perror("lseek failed");
            readSuccess = false;
            break;
        }

        ssize_t bytesRead = read(fd, buffer + bufferOffset, bytesToRead);
        if (bytesRead < 0) {
            std::perror("read failed");
            readSuccess = false;
            break;
        }
        if (static_cast<size_t>(bytesRead) != bytesToRead) {
            std::cerr << "Incomplete read: expected " << bytesToRead << " bytes, got " << bytesRead << std::endl;
            readSuccess = false;
            break;
        }
    }

    close(fdCrit);
    close(fdNonCrit);

    return readSuccess ? ResultCode::SUCCESS : ResultCode::FAILURE;
}

ResultCode AbstractFileHandler::writeFile(const char* mappingPath, const char* buffer, size_t size, off_t offset) {
    // Derive base path
    std::string basePath(mappingPath);
    const std::string mappingSuffix = ".mapping";
    if (basePath.size() <= mappingSuffix.size() || basePath.substr(basePath.size() - mappingSuffix.size()) != mappingSuffix) {
        std::cerr << "Invalid mappingPath: missing .mapping suffix\n";
        return ResultCode::FAILURE;
    }
    basePath = basePath.substr(0, basePath.size() - mappingSuffix.size());

    std::string critPath = basePath + ".crit";
    std::string noncritPath = basePath + ".noncrit";

    bool mappingExists = std::ifstream(mappingPath).good();
    std::vector<char> mergedBuffer;

    if (mappingExists) {
        // Load existing mapping (mappingfile -> std::map)
        if (loadMapFromFile(mappingPath) != ResultCode::SUCCESS) {
            std::cerr << "Failed to load existing mapping\n";
            return ResultCode::FAILURE;
        }

        // Reconstruct full logical file
        int totalSize = 0;
        for (const auto& [range, _] : fileMap) {
            totalSize = std::max(totalSize, range.getEnd() + 1);
        }
        mergedBuffer.resize(totalSize, 0);

        // if the file is empty, we don't need to read anything
        if(totalSize != 0) {
        
            if (readFile(mappingPath, mergedBuffer.data(), totalSize, 0) != ResultCode::SUCCESS) {
                std::cerr << "Failed to reconstruct existing data\n";
                return ResultCode::FAILURE;
            }

            // Merge new buffer
            if (offset + size > mergedBuffer.size()) {
                mergedBuffer.resize(offset + size, 0);
            }
            std::memcpy(mergedBuffer.data() + offset, buffer, size);
        }

    } else {
        // New mapping
        mergedBuffer.resize(offset + size, 0);
        std::memcpy(mergedBuffer.data() + offset, buffer, size);
    }

    // Re-analyze and split into critical/non-critical data
    fileMap.clear(); // Clear existing map for regeneration
    std::vector<char> critData; // buffer for critical data
    std::vector<char> noncritData; // buffer for non-critical data

    if (createMapping(mergedBuffer.data(), mergedBuffer.size()) != ResultCode::SUCCESS) {
        std::cerr << "Critical analysis failed\n";
        return ResultCode::FAILURE;
    }

    // fill in the critical and non-critical data vectors
    for (const auto& [range, mappedPair] : fileMap) {
        const Range& mappedRange = mappedPair.first;
        CriticalType type = mappedPair.second;

        if (type == CriticalType::CRITICAL_DATA) {
            critData.insert(critData.end(), mergedBuffer.begin() + range.getStart(), mergedBuffer.begin() + range.getEnd() + 1);
        } else {
            noncritData.insert(noncritData.end(), mergedBuffer.begin() + range.getStart(), mergedBuffer.begin() + range.getEnd() + 1);
        }
    }

    // Write critical data
    {
        std::ofstream critFile(critPath, std::ios::binary | std::ios::trunc);
        if (!critFile.is_open()) {
            std::cerr << "Failed to open .crit file for writing\n";
            return ResultCode::FAILURE;
        }
        critFile.write(critData.data(), critData.size());
    }

    // Write non-critical data
    {
        std::ofstream noncritFile(noncritPath, std::ios::binary | std::ios::trunc);
        if (!noncritFile.is_open()) {
            std::cerr << "Failed to open .noncrit file for writing\n";
            return ResultCode::FAILURE;
        }
        noncritFile.write(noncritData.data(), noncritData.size());
    }

    // Save updated mapping
    if (saveMapToFile(mappingPath) != ResultCode::SUCCESS) {
        std::cerr << "Failed to save mapping file\n";
        return ResultCode::FAILURE;
    }

    return ResultCode::SUCCESS;
}

