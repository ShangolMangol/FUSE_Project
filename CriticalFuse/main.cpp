#include "FileHandlers/TextFile.h"
#include <iostream>
#include <string>
#include <cstring>

void printBuffer(const char* buffer, size_t size) {
    std::cout << "Buffer content: ";
    for (size_t i = 0; i < size; i++) {
        std::cout << buffer[i];
    }
    std::cout << std::endl;
}

int main() {
    // Create a TextFileHandler instance
    TextFileHandler handler;

    // Test mapping file path
    const char* mappingPath = "test.txt.mapping";

    // Test data
    const char* testData = "Hello, this is a test file with some content that will be split into critical and non-critical data.";
    size_t dataSize = strlen(testData);

    std::cout << "Original data: " << testData << std::endl;
    std::cout << "Data size: " << dataSize << " bytes" << std::endl;

    // Write the data
    std::cout << "\nWriting data..." << std::endl;
    if (handler.writeFile(mappingPath, testData, dataSize, 0) != ResultCode::SUCCESS) {
        std::cerr << "Failed to write file" << std::endl;
        return 1;
    }

    // Read the data back
    std::cout << "\nReading data back..." << std::endl;
    char* readBuffer = new char[dataSize + 1];  // +1 for null terminator
    memset(readBuffer, 0, dataSize + 1);

    if (handler.readFile(mappingPath, readBuffer, dataSize, 0) != ResultCode::SUCCESS) {
        std::cerr << "Failed to read file" << std::endl;
        delete[] readBuffer;
        return 1;
    }

    // Print the read data
    std::cout << "Read data: " << readBuffer << std::endl;

    // Verify the data matches
    if (strcmp(testData, readBuffer) == 0) {
        std::cout << "\nData verification successful!" << std::endl;
    } else {
        std::cout << "\nData verification failed!" << std::endl;
    }

    // Clean up
    delete[] readBuffer;

    return 0;
} 