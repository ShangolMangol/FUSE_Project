#ifndef FILE_HANDLERS_HPP
#define FILE_HANDLERS_HPP


#include <string>
#include <map>
#include <utility>
#include "../Utilities/Range.h"

enum class CriticalType {
    CRITICAL_DATA = 0,
    NON_CRITICAL_DATA = 1
};

class AbstractFileHandler {
private:
    const char* filePath; 
    std::map<Range, std::pair<Range, CriticalType>> fileMap; // map of file ranges to critical types
public:
    AbstractFileHandler();

    virtual ~AbstractFileHandler() = default;   // destructor
    
};

#endif
