#ifndef JPEG_FILE_HANDLERS_HPP
#define JPEG_FILE_HANDLERS_HPP

#include "AbstractFile.h"


class JpegFileHandler : public AbstractFileHandler {
public:
    JpegFileHandler() = default; // default constructor
    JpegFileHandler(const JpegFileHandler&) = default; // copy constructor
    ~JpegFileHandler() override = default; // destructor

    ResultCode createMapping(const char* buffer, size_t size) override;
};

#endif // JPEG_FILE_HANDLERS_HPP