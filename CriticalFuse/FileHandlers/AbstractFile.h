#ifndef FILE_HANDLERS_HPP
#define FILE_HANDLERS_HPP


#include <string>
#include <map>
#include <utility>
#include <vector>
#include "../Utilities/Range.h"

enum class CriticalType {
    CRITICAL_DATA = 0,
    NON_CRITICAL_DATA = 1
};

enum class ResultCode {
    SUCCESS = 0,
    FAILURE = 1
};

class AbstractFileHandler {
private:
    std::map<Range, std::pair<Range, CriticalType>> fileMap; // map of file ranges to critical types
    

public:
    AbstractFileHandler() = default; // default constructor
    AbstractFileHandler(const AbstractFileHandler&) = default; // copy constructor
    virtual ~AbstractFileHandler() = default;   // destructor

    std::map<Range, std::pair<Range, CriticalType>>& getFileMap(); // getter for fileMap
    ResultCode setFileMap(const std::map<Range, std::pair<Range, CriticalType>>& newFileMap); // setter for fileMap
    
   
    /**
     * @brief Adds a mapping to the fileMap with the given ranges and critical type.
     * 
     * @param origStart original start index
     * @param origEnd original end index
     * @param mappedStart mapped start index
     * @param mappedEnd mapped end index
     * @param type critical type (CRITICAL_DATA or NON_CRITICAL_DATA)
     * @return ResultCode SUCCESS if successful, FAILURE otherwise
     */
    ResultCode addToFileMap(int origStart, int origEnd, int mappedStart, int mappedEnd, CriticalType type);

    /**
     * @brief Loads a mapping memory file from the given path and populates the fileMap with its contents.
     * 
     * @param mappingPath the path to the mapping file
     * @return ResultCode SUCCESS if successful, FAILURE otherwise
     */
    ResultCode loadMapFromFile(const char* mappingPath); 

    /**
     * @brief Saves the current fileMap to a mapping memory file at the given path.
     * 
     * @param mappingPath the path to the mapping file
     * @return ResultCode SUCCESS if successful, FAILURE otherwise
     */
    ResultCode saveMapToFile(const char* mappingPath); 

    /**
     * @brief Reads a file from the given path and writes to the buffer with its contents.
     * 
     * @param mappingPath - the path to the mapping file - to know where the content is stored
     * @param buffer buffer to read into
     * @param size size of the buffer
     * @param offset offset to read from
     * @return ResultCode SUCCESS if successful, FAILURE otherwise
     */
    ResultCode readFile(const char* mappingPath, char* buffer, size_t size, off_t offset);

    /**
     * @brief Writes the given buffer to a file at the given path.
     * 
     * @param mappingPath - the path to the mapping file - to know where the content is stored
     * @param buffer buffer to write from
     * @param size size of the buffer
     * @param offset offset to write to
     * @return ResultCode SUCCESS if successful, FAILURE otherwise
     */
    ResultCode writeFile(const char* mappingPath, const char* buffer, size_t size, off_t offset); 

    /**
     * @brief Create a Mapping for critical data and non-critical data in the file, saved in the map object.
     * 
     * IMPORTANT: This function is virtual and should be implemented in the derived classes.
     * Also changes fileMap, critData, and noncritData.
     * 
     * @param buffer the buffer to create the mapping from, containing the whole file content
     * @param size the size of the buffer
     * @return ResultCode SUCCESS if successful, FAILURE otherwise
     */
    virtual ResultCode createMapping(const char* buffer, size_t size) = 0; 

    /**
     * @brief Reads the entire file into the buffer.
     * 
     * @param mappingPath the path to the mapping file
     * @param buffer buffer to read into
     * @return ResultCode SUCCESS if successful, FAILURE otherwise
     */
    ResultCode readFullFile(const char* mappingPath, char* buffer); 

    
};

#endif
