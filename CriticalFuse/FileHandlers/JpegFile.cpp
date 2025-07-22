// Full C++ class using libjpeg to split JPEG into AC and critical (DC + headers) parts
// Requires libjpeg (e.g., libjpeg-turbo)

#include "JpegFile.h"
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
        // save the buffer to a file - temporary file
        std::ofstream file(basePath, std::ios::binary);
        file.write(buffer, size);
        file.close();

        // split the file into .crit and .noncrit using GuetzliSplit
        std::string command = "/home/shangol-mangol/Desktop/FuseProject/FUSE_Project/CriticalFuse/GuetzliSplit --split " + basePath + " " + basePath + ".crit ";
        std::cout << "Executing split command: " << command << std::endl;
        char cwd[1024]; getcwd(cwd, sizeof(cwd));
        std::cout << "Current directory: " << cwd << std::endl;
        int ret = system(command.c_str());
        if (ret != 0) {
            return ResultCode::FAILURE;
        }

        // remove the temporary file
        // unlink(basePath.c_str());
    }
    return ResultCode::SUCCESS;
}

ResultCode JpegFileHandler::readFile(const char* mappingPath, char* buffer, size_t size, off_t offset) {
    std::string basePath(mappingPath);
    const std::string suffix = ".mapping";
    if (basePath.size() >= suffix.size() && basePath.compare(basePath.size() - suffix.size(), suffix.size(), suffix) == 0) {
        basePath = basePath.substr(0, basePath.size() - suffix.size());
    }
    //remove the .jpg from the basePath
    basePath = basePath.substr(0, basePath.size() - 4);

    // merge the .crit and .noncrit files back into a JPEG
    std::string command = "/home/shangol-mangol/Desktop/FuseProject/FUSE_Project/CriticalFuse/GuetzliSplit --merge " + basePath + ".crit " + basePath + ".jpg";
    std::cout << "Executing merge command: " << command << std::endl;
    char cwd[1024]; getcwd(cwd, sizeof(cwd));
    std::cout << "Current directory: " << cwd << std::endl;
    int ret = system(command.c_str());
    if (ret != 0) {
        return ResultCode::FAILURE;
    }

    // read the JPEG file into the buffer
    std::ifstream mergedImage(basePath + ".jpg", std::ios::binary);
    int mergedImageSize = mergedImage.tellg();
    mergedImage.seekg(0, std::ios::beg);
    mergedImage.read(buffer, mergedImageSize);
    std::cout << "Reading from file: " << basePath + ".jpg" << " with size: " << mergedImageSize << std::endl;

    mergedImage.close();

    // remove the temporary file
    // unlink(basePath + ".jpg");

    return ResultCode::SUCCESS;
}

ResultCode JpegFileHandler::createMapping(const char* buffer, size_t size) {
    // For this approach, mapping is not used, so just return SUCCESS
    return ResultCode::SUCCESS;
}
