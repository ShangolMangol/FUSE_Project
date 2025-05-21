#ifndef DNG_FILE_HANDLERS_HPP
#define DNG_FILE_HANDLERS_HPP

#include "AbstractFile.h"

class DngFileHandler : public AbstractFileHandler {
public:
    DngFileHandler() = default; // default constructor
    DngFileHandler(const DngFileHandler&) = default; // copy constructor
    ~DngFileHandler() override = default; // destructor
    
    ResultCode createMapping(const char* buffer, size_t size) override;
};

#endif // DNG_FILE_HANDLERS_HPP 