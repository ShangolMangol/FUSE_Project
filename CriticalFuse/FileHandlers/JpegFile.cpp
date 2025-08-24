// Full C++ class using integrated Guetzli functionality to split JPEG into AC and critical (DC + headers) parts

#include "JpegFile.h"
#include "../GuetzliSplit/guetzli_buffer.h"
#include <vector>
#include <fstream>
#include <iostream>
#include <cstring>
#include <string>
#include <unistd.h>

ResultCode JpegFileHandler::writeFile(const char* mappingPath, const char* buffer, size_t size, off_t offset) {
    std::string basePath(mappingPath);
    const std::string suffix = ".mapping";
    if (basePath.size() >= suffix.size() && basePath.compare(basePath.size() - suffix.size(), suffix.size(), suffix) == 0) {
        basePath = basePath.substr(0, basePath.size() - suffix.size());
    }

    if (offset == 0) {
        // Convert buffer to string for Guetzli processing
        std::string jpeg_data(buffer, size);
        
        // Split the JPEG using integrated Guetzli functionality
        std::string critical_data, noncritical_data;
        if (!guetzli::SplitJpegBuffer(jpeg_data, critical_data, noncritical_data, basePath)) {
            std::cerr << "Failed to split JPEG using Guetzli" << std::endl;
            return ResultCode::FAILURE;
        }

        // Write critical part to .crit file
        std::string critPath = basePath + ".crit";
        std::ofstream critFile(critPath, std::ios::binary);
        if (!critFile.is_open()) {
            std::cerr << "Failed to open critical file for writing: " << critPath << std::endl;
            return ResultCode::FAILURE;
        }
        critFile.write(critical_data.data(), critical_data.size());
        critFile.close();

        // Write non-critical part to .noncrit file
        std::string noncritPath = basePath + ".noncrit";
        std::ofstream noncritFile(noncritPath, std::ios::binary);
        if (!noncritFile.is_open()) {
            std::cerr << "Failed to open non-critical file for writing: " << noncritPath << std::endl;
            return ResultCode::FAILURE;
        }
        noncritFile.write(noncritical_data.data(), noncritical_data.size());
        noncritFile.close();

        // Create mapping file with size information
        std::ofstream mappingFile(mappingPath);
        mappingFile << "size: " << size << std::endl;
        mappingFile.close();

        std::cout << "Successfully split JPEG into critical (" << critical_data.size() 
                  << " bytes) and non-critical (" << noncritical_data.size() << " bytes) parts" << std::endl;
    }
    return ResultCode::SUCCESS;
}

ResultCode JpegFileHandler::readFile(const char* mappingPath, char* buffer, size_t& size, off_t offset) {
    std::string basePath(mappingPath);
    const std::string suffix = ".mapping";
    if (basePath.size() >= suffix.size() && basePath.compare(basePath.size() - suffix.size(), suffix.size(), suffix) == 0) {
        basePath = basePath.substr(0, basePath.size() - suffix.size());
    }

    // Read critical part
    std::string critPath = basePath + ".crit";
    std::ifstream critFile(critPath, std::ios::binary);
    if (!critFile.is_open()) {
        std::cerr << "Failed to open critical file: " << critPath << std::endl;
        return ResultCode::FAILURE;
    }
    std::stringstream critBuffer;
    critBuffer << critFile.rdbuf();
    std::string critical_data = critBuffer.str();
    critFile.close();

    // Read non-critical part
    std::string noncritPath = basePath + ".noncrit";
    std::ifstream noncritFile(noncritPath, std::ios::binary);
    if (!noncritFile.is_open()) {
        std::cerr << "Failed to open non-critical file: " << noncritPath << std::endl;
        return ResultCode::FAILURE;
    }
    std::stringstream noncritBuffer;
    noncritBuffer << noncritFile.rdbuf();
    std::string noncritical_data = noncritBuffer.str();
    noncritFile.close();

    // Merge the parts using integrated Guetzli functionality
    std::string jpeg_data;
    if (!guetzli::MergeJpegBuffer(critical_data, noncritical_data, jpeg_data, basePath)) {
        std::cerr << "Failed to merge JPEG using Guetzli" << std::endl;
        return ResultCode::FAILURE;
    }

    // Copy merged data to buffer
    if (jpeg_data.size() > size) {
        std::cerr << "Buffer too small for merged JPEG data" << std::endl;
        return ResultCode::FAILURE;
    }
    
    memcpy(buffer, jpeg_data.data(), jpeg_data.size());
    size = jpeg_data.size();

    std::cout << "Successfully merged JPEG from critical (" << critical_data.size() 
              << " bytes) and non-critical (" << noncritical_data.size() 
              << " bytes) parts into " << size << " bytes" << std::endl;

    return ResultCode::SUCCESS;
}

ResultCode JpegFileHandler::createMapping(const char* buffer, size_t size) {
    // For this approach, mapping is not used, so just return SUCCESS
    return ResultCode::SUCCESS;
}
