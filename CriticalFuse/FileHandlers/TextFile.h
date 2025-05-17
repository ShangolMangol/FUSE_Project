#ifndef TEXT_FILE_HANDLERS_HPP
#define TEXT_FILE_HANDLERS_HPP

#include "AbstractFile.h"

class TextFileHandler : public AbstractFileHandler {
    public:
        TextFileHandler() = default; // default constructor
        TextFileHandler(const TextFileHandler&) = default; // copy constructor
        virtual ~TextFileHandler() override = default;   // destructor

        ResultCode createMapping(const char* buffer, size_t size) override;
};


