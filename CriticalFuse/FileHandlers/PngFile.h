#ifndef PNG_FILE_HANDLERS_HPP
#define PNG_FILE_HANDLERS_HPP

#include "AbstractFile.h"

class PngFileHandler : public AbstractFileHandler {
public:
    PngFileHandler() = default; // default constructor
    PngFileHandler(const PngFileHandler&) = default; // copy constructor
    ~PngFileHandler() override = default; // destructor

    ResultCode createMapping(const char* buffer, size_t size) override;
};

#endif // PNG_FILE_HANDLERS_HPP