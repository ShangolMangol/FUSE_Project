#ifndef BMP_FILE_HANDLERS_HPP
#define BMP_FILE_HANDLERS_HPP

#include "AbstractFile.h"

class BmpFileHandler : public AbstractFileHandler {
public:
    BmpFileHandler() = default; // default constructor
    BmpFileHandler(const BmpFileHandler&) = default; // copy constructor
    ~BmpFileHandler() override = default; // destructor

    ResultCode createMapping(const char* buffer, size_t size) override;
};


#endif // BMP_FILE_HANDLERS_HPP

