#ifndef JPEG_FILE_HANDLERS_HPP
#define JPEG_FILE_HANDLERS_HPP

#include "AbstractFile.h"
#include <vector>
#include <utility>

class JpegFileHandler : public AbstractFileHandler {
public:
    JpegFileHandler() = default; // default constructor
    JpegFileHandler(const JpegFileHandler&) = default; // copy constructor
    ~JpegFileHandler() override = default; // destructor

    ResultCode readFile(const char* mappingPath, char* buffer, size_t size, off_t offset) override;
    ResultCode writeFile(const char* mappingPath, const char* buffer, size_t size, off_t offset) override;
    ResultCode createMapping(const char* buffer, size_t size) override;

};

#endif // JPEG_FILE_HANDLERS_HPP