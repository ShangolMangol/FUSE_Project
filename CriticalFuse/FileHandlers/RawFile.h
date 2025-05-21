#ifndef RAW_FILE_HANDLERS_HPP
#define RAW_FILE_HANDLERS_HPP

#include "AbstractFile.h"

class RawFileHandler : public AbstractFileHandler {
public:
    RawFileHandler() = default; // default constructor
    RawFileHandler(const RawFileHandler&) = default; // copy constructor
    ~RawFileHandler() override = default; // destructor
    
    ResultCode createMapping(const char* path, size_t size) override;

       
};


#endif // RAW_FILE_HANDLERS_HPP