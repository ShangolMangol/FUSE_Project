#define FUSE_USE_VERSION 31
#define _GNU_SOURCE

#include <fuse3/fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>
#include <limits.h>
#include <memory>
#include <string>
#include <map>
#include <iostream>
#include <fstream>
#include <cstring>

#include "../FileHandlers/AbstractFile.h"
#include "../FileHandlers/TextFile.h"
#include "../FileHandlers/RawFile.h"
#include "../FileHandlers/DngFile.h"
#include "../FileHandlers/PngFile.h"
#include "../FileHandlers/BmpFile.h"
#include "../FileHandlers/JpegFile.h"

#define BACKING_DIR_REL "./storage"
static char backing_dir_abs[PATH_MAX];

// Buffer for handling large file writes
struct FileWriteBuffer {
    std::vector<char> data;
    size_t total_size;
};

// Buffer for handling file reads (caches entire file on first read)
struct FileReadBuffer {
    std::vector<char> data;
    size_t total_size;
    bool loaded;
};

static std::map<std::string, FileWriteBuffer> write_buffers;
static std::map<std::string, FileReadBuffer> read_buffers;

// FUSE attribute flags
#define FUSE_SET_ATTR_MODE  (1 << 0)
#define FUSE_SET_ATTR_UID   (1 << 1)
#define FUSE_SET_ATTR_GID   (1 << 2)
#define FUSE_SET_ATTR_SIZE  (1 << 3)
#define FUSE_SET_ATTR_ATIME (1 << 4)
#define FUSE_SET_ATTR_MTIME (1 << 5)

// Helper to construct the full path in the backing directory
static void fullpath(char fpath[PATH_MAX], const char *path) {
    if (strcmp(path, "/") == 0) {
        snprintf(fpath, PATH_MAX, "%s", backing_dir_abs);
    } else {
        snprintf(fpath, PATH_MAX, "%s%s", backing_dir_abs, path);
    }
}

// Helper to get file handler based on file type
static std::unique_ptr<AbstractFileHandler> getFileHandler(const char* path) {
    // Get file extension
    const char* ext = strrchr(path, '.');
    if (!ext) {
        // No extension, treat as regular file
        return nullptr;
    }
    
    // Convert extension to lowercase for comparison
    std::string ext_lower(ext + 1);
    for (char& c : ext_lower) {
        c = std::tolower(c);
    }
    std::cout << "File extension: " << ext_lower << std::endl;
    // Only handle specific file types
    if (ext_lower == "txt") {
        return std::make_unique<TextFileHandler>();
    }
    else if (ext_lower == "dng") {
        return std::make_unique<DngFileHandler>();
    }
    else if (ext_lower == "png") {
        return std::make_unique<PngFileHandler>();
    }
    else if (ext_lower == "bmp") {
        return std::make_unique<BmpFileHandler>();
    }
    else if (ext_lower == "jpeg" || ext_lower == "jpg") {
        return std::make_unique<JpegFileHandler>();
    }
    
    // Unsupported file type, treat as regular file
    return nullptr;
}

// Helper function to update file times
static int update_times(const char* path, struct stat* stbuf, int to_set) {
    struct timespec ts[2];
    ts[0].tv_sec = 0;
    ts[0].tv_nsec = UTIME_OMIT;
    ts[1].tv_sec = 0;
    ts[1].tv_nsec = UTIME_OMIT;

    if (to_set & FUSE_SET_ATTR_ATIME) {
        ts[0] = stbuf->st_atim;
    }
    if (to_set & FUSE_SET_ATTR_MTIME) {
        ts[1] = stbuf->st_mtim;
    }

    int res = utimensat(AT_FDCWD, path, ts, 0);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

// FUSE operations
static int criticalfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi;
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    std::cout << "Getting attributes for file: " << fpath << std::endl;
    // Check for mapping file first
    std::string mappingPath = std::string(fpath) + ".mapping";
    if (access(mappingPath.c_str(), F_OK) == 0) {
        // It's a critical file, get its size from the mapping
        auto handler = getFileHandler(path);
        if (!handler) {
            // Not a supported file type, treat as regular file
            return -ENOENT;
        }
        if (handler->loadMapFromFile(mappingPath.c_str()) != ResultCode::SUCCESS) {
            std::cerr << "Failed to load mapping from file: " << mappingPath << std::endl;
            return -errno;
        }

        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode = S_IFREG | 0644;
        stbuf->st_nlink = 1;

        // Calculate total size from mapping
        int totalSize = 0;
        for (const auto& [range, _] : handler->getFileMap()) {
            totalSize = std::max(totalSize, range.getEnd() + 1);
        }
        if (totalSize == 0 && access(mappingPath.c_str(), F_OK) == 0) {
            std::cout << "Total size is 0, reading from mapping file" << std::endl;
            std::ifstream mappingFile(mappingPath.c_str());
            std::string line;
            std::getline(mappingFile, line);
            if (line.find("size:") != std::string::npos) {
                totalSize = std::stoi(line.substr(5));
            }
            else {
                std::cout << "Mapping file does not contain size" << std::endl;  
            }
            mappingFile.close();
        }
        stbuf->st_size = totalSize;

        return 0;
    }

    // Not a critical file, check if it's a regular file or directory
    int res = lstat(fpath, stbuf);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

static int criticalfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                            off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    (void) offset;
    (void) fi;
    (void) flags;

    char fpath[PATH_MAX];
    fullpath(fpath, path);

    DIR *dp = opendir(fpath);
    if (!dp) {
        return -errno;
    }

    filler(buf, ".", NULL, 0, (fuse_fill_dir_flags)0);
    filler(buf, "..", NULL, 0, (fuse_fill_dir_flags)0);

    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        const char *name = de->d_name;
        
        // Skip system entries and mapping/critical files
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
            strstr(name, ".crit") || strstr(name, ".noncrit")) {
            continue;
        }

        if(strstr(name, ".mapping")) {
            //show without the ".mapping" suffix
            char nameWithoutMapping[PATH_MAX];
            strncpy(nameWithoutMapping, name, strlen(name) - strlen(".mapping"));
            nameWithoutMapping[strlen(name) - strlen(".mapping")] = '\0';
            name = nameWithoutMapping;
        }

        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;
        
        if (filler(buf, name, &st, 0, (fuse_fill_dir_flags)0)) {
            break;
        }
    }

    closedir(dp);
    return 0;
}

static int criticalfs_open(const char *path, struct fuse_file_info *fi) {
    (void) path;
    (void) fi;
    std::cout << "Opening file: " << path << std::endl;
    return 0;
}

static int criticalfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) fi;
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    std::cout << "Reading from file: " << fpath << std::endl;
    
    std::string fileKey = std::string(path);
    auto& readBuffer = read_buffers[fileKey];
    
    // Check if this is a critical file
    std::string mappingPath = std::string(fpath) + ".mapping";
    if (access(mappingPath.c_str(), F_OK) == 0) {
        auto handler = getFileHandler(path);
        if (!handler) {
            // Not a supported file type, treat as regular file
            return -ENOENT;
        }
        
        // Load file into buffer on first read
        if (!readBuffer.loaded) {
            std::cout << "First read of critical file - loading into buffer" << std::endl;
            
            // Get file size first
            struct stat st;
            if (criticalfs_getattr(path, &st, NULL) != 0) {
                return -errno;
            }
            readBuffer.total_size = st.st_size;
            
            // Resize buffer to accommodate entire file
            readBuffer.data.resize(readBuffer.total_size);
            
            // Read entire file using the handler
            if (handler->readFile(mappingPath.c_str(), readBuffer.data.data(), readBuffer.total_size, 0) != ResultCode::SUCCESS) {
                std::cerr << "Failed to load file into read buffer" << std::endl;
                return -errno;
            }
            
            readBuffer.loaded = true;
            std::cout << "Successfully loaded " << readBuffer.total_size << " bytes into read buffer" << std::endl;
        }
        
        // Read from buffer
        size_t bytesToRead = std::min(size, readBuffer.total_size - offset);
        if (bytesToRead > 0) {
            memcpy(buf, readBuffer.data.data() + offset, bytesToRead);
            std::cout << "Read " << bytesToRead << " bytes from buffer at offset " << offset << std::endl;
            return bytesToRead;
        }
        
        return 0; // End of file
    }

    // Not a critical file, handle with read buffer
    if (!readBuffer.loaded) {
        std::cout << "First read of regular file - loading into buffer" << std::endl;
        
        // Get file size
        struct stat st;
        if (criticalfs_getattr(path, &st, NULL) != 0) {
            return -errno;
        }
        readBuffer.total_size = st.st_size;
        
        // Resize buffer to accommodate entire file
        readBuffer.data.resize(readBuffer.total_size);
        
        // Read entire file
        int fd = open(fpath, O_RDONLY);
        if (fd == -1) {
            return -errno;
        }
        
        ssize_t bytesRead = read(fd, readBuffer.data.data(), readBuffer.total_size);
        close(fd);
        
        if (bytesRead == -1) {
            return -errno;
        }
        
        readBuffer.total_size = bytesRead;
        readBuffer.loaded = true;
        std::cout << "Successfully loaded " << readBuffer.total_size << " bytes into read buffer" << std::endl;
    }
    
    // Read from buffer
    size_t bytesToRead = std::min(size, readBuffer.total_size - offset);
    if (bytesToRead > 0) {
        memcpy(buf, readBuffer.data.data() + offset, bytesToRead);
        std::cout << "Read " << bytesToRead << " bytes from buffer at offset " << offset << std::endl;
        return bytesToRead;
    }
    
    return 0; // End of file
}

static int criticalfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) fi;
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    std::cout << "Writing to file: " << fpath << " with size: " << size << " at offset: " << offset << std::endl;
    
    std::string fileKey = std::string(path);
    
    // Invalidate read buffer when file is being written to
    auto readIt = read_buffers.find(fileKey);
    if (readIt != read_buffers.end()) {
        readIt->second.loaded = false;
        readIt->second.data.clear();
        std::cout << "Invalidated read buffer for file: " << path << std::endl;
    }
    
    // Check if this is a critical file
    std::string mappingPath = std::string(fpath) + ".mapping";
    if (access(mappingPath.c_str(), F_OK) == 0) {
        auto& buffer = write_buffers[fileKey];
        
        // Ensure buffer is large enough
        if (buffer.data.size() < offset + size) {
            buffer.data.resize(offset + size);
        }
        
        // Copy data to buffer
        memcpy(buffer.data.data() + offset, buf, size);
        buffer.total_size = std::max(buffer.total_size, offset + size);
        
        // Just buffer the data - processing will happen in release() when file is closed
        std::cout << "Buffered " << size << " bytes at offset " << offset << " (total buffered: " << buffer.total_size << ")" << std::endl;
        
        return size;
    }
    
    std::cout << "Not a critical file, writing directly" << std::endl;
    // Not a critical file, write directly
    int fd = open(fpath, O_WRONLY);
    if (fd == -1) {
        return -errno;
    }

    int res = pwrite(fd, buf, size, offset);
    close(fd);
    if (res == -1) {
        return -errno;
    }
    return res;
}

static int criticalfs_release(const char *path, struct fuse_file_info *fi) {
    (void) fi;
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    
    // Check if there's buffered data for this file that needs to be processed
    std::string fileKey = std::string(path);
    auto it = write_buffers.find(fileKey);
    if (it != write_buffers.end()) {
        std::string mappingPath = std::string(fpath) + ".mapping";
        if (access(mappingPath.c_str(), F_OK) == 0) {
            auto handler = getFileHandler(path);
            if (handler && it->second.total_size > 0) {
                std::cout << "File closed - processing complete buffered file: " << it->second.total_size << " bytes" << std::endl;
                ResultCode result = handler->writeFile(mappingPath.c_str(), it->second.data.data(), it->second.total_size, 0);
                if (result == ResultCode::SUCCESS) {
                    std::cout << "Successfully processed complete file of size: " << it->second.total_size << " bytes" << std::endl;
                    // Invalidate read buffer after successful write
                    auto readIt = read_buffers.find(fileKey);
                    if (readIt != read_buffers.end()) {
                        readIt->second.loaded = false;
                        readIt->second.data.clear();
                        std::cout << "Invalidated read buffer after file write" << std::endl;
                    }
                } else {
                    std::cerr << "Failed to process file of size: " << it->second.total_size << " bytes" << std::endl;
                }
            }
        }
        write_buffers.erase(it);
    }
    
    return 0;
}

static int criticalfs_flush(const char *path, struct fuse_file_info *fi) {
    (void) fi;
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    
    // Check if there's buffered data that should be processed on flush
    std::string fileKey = std::string(path);
    auto it = write_buffers.find(fileKey);
    if (it != write_buffers.end()) {
        std::string mappingPath = std::string(fpath) + ".mapping";
        if (access(mappingPath.c_str(), F_OK) == 0) {
            auto handler = getFileHandler(path);
            if (handler && it->second.total_size > 0) {
                std::cout << "Flush called - processing buffered file: " << it->second.total_size << " bytes" << std::endl;
                ResultCode result = handler->writeFile(mappingPath.c_str(), it->second.data.data(), it->second.total_size, 0);
                if (result == ResultCode::SUCCESS) {
                    std::cout << "Successfully processed file on flush: " << it->second.total_size << " bytes" << std::endl;
                    write_buffers.erase(it); // Clean up buffer after successful processing
                }
            }
        }
    }
    
    return 0;
}

static int criticalfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    std::cout << "Creating file: " << fpath << std::endl;
    // Only create mapping for supported file types
    auto handler = getFileHandler(path);
    if (handler) {
        std::string mappingPath = std::string(fpath) + ".mapping";
        if (handler->createMapping("", 0) != ResultCode::SUCCESS) {
            unlink(fpath); // Clean up the created file
            std::cerr << "Failed to create mapping for file: " << path << std::endl;
            return -errno;
        }
        std::cout << "Mapping file created" << std::endl;
        if (handler->saveMapToFile(mappingPath.c_str()) != ResultCode::SUCCESS) {
            unlink(fpath); // Clean up the created file
            std::cerr << "Failed to save mapping file: " << mappingPath << std::endl;
            return -errno;
        }
        std::cout << "Saved mapping file: " << mappingPath << std::endl;

        return 0;
    }
    std::cout << "Not a critical file, creating normally" << std::endl;
    // Create the file normally if not a critical file
    int fd = open(fpath, fi->flags | O_CREAT, mode);
    if (fd == -1) {
        return -errno;
    }
    close(fd);

    return 0;
}

static int criticalfs_unlink(const char *path) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    std::cout << "Unlinking file: " << fpath << std::endl;
    
    // Clear read buffer for this file
    std::string fileKey = std::string(path);
    auto readIt = read_buffers.find(fileKey);
    if (readIt != read_buffers.end()) {
        read_buffers.erase(readIt);
        std::cout << "Cleared read buffer for deleted file: " << path << std::endl;
    }
    
    // Check if this is a critical file
    auto handler = getFileHandler(path);
    if (handler) {
        std::string mappingPath = std::string(fpath) + ".mapping";
        if (access(mappingPath.c_str(), F_OK) == 0) {
            // It's a critical file, remove the mapping and data files
            unlink(mappingPath.c_str());
            std::string critPath = std::string(fpath) + ".crit";
            std::string noncritPath = std::string(fpath) + ".noncrit";
            unlink(critPath.c_str());
            unlink(noncritPath.c_str());
            unlink(fpath);
        }
        return 0;

    }

    // Not a critical file, remove normally
    int res = unlink(fpath);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

static int criticalfs_mkdir(const char *path, mode_t mode) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    int res = mkdir(fpath, mode);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

static int criticalfs_rmdir(const char *path) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    int res = rmdir(fpath);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

static int criticalfs_rename(const char *from, const char *to, unsigned int flags) {
    if (flags) return -EINVAL;

    char from_path[PATH_MAX], to_path[PATH_MAX];
    fullpath(from_path, from);
    fullpath(to_path, to);

    // Handle read buffers for renamed files
    std::string fromKey = std::string(from);
    std::string toKey = std::string(to);
    
    auto fromReadIt = read_buffers.find(fromKey);
    if (fromReadIt != read_buffers.end()) {
        // Move read buffer from old path to new path
        read_buffers[toKey] = std::move(fromReadIt->second);
        read_buffers.erase(fromReadIt);
        std::cout << "Moved read buffer from " << from << " to " << to << std::endl;
    }

    // Handle critical file components
    std::string fromMapping = std::string(from_path) + ".mapping";
    std::string fromCrit = std::string(from_path) + ".crit";
    std::string fromNoncrit = std::string(from_path) + ".noncrit";
    std::string toMapping = std::string(to_path) + ".mapping";
    std::string toCrit = std::string(to_path) + ".crit";
    std::string toNoncrit = std::string(to_path) + ".noncrit";

    rename(fromMapping.c_str(), toMapping.c_str());
    rename(fromCrit.c_str(), toCrit.c_str());
    rename(fromNoncrit.c_str(), toNoncrit.c_str());

    // Rename the main file
    int res = rename(from_path, to_path);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

static int criticalfs_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    (void) fi;
    char fpath[PATH_MAX];
    fullpath(fpath, path);

    // Clear read buffer when file is truncated
    std::string fileKey = std::string(path);
    auto readIt = read_buffers.find(fileKey);
    if (readIt != read_buffers.end()) {
        readIt->second.loaded = false;
        readIt->second.data.clear();
        std::cout << "Invalidated read buffer for truncated file: " << path << std::endl;
    }

    // Check if this is a critical file
    std::string mappingPath = std::string(fpath) + ".mapping";
    if (access(mappingPath.c_str(), F_OK) == 0) {
        // For critical files, we'll allow truncate to 0 (which is what happens when moving to trash)
        if (size == 0) {
            return 0;
        }
        return -EACCES;  // Don't allow other truncations for critical files
    }

    // For regular files, use normal truncate
    int res = truncate(fpath, size);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

// static int criticalfs_chmod(const char *path, mode_t mode, struct fuse_file_info *fi) {
//     (void) fi;
//     char fpath[PATH_MAX];
//     fullpath(fpath, path);

//     // Check if this is a critical file
//     std::string mappingPath = std::string(fpath) + ".mapping";
//     bool is_critical = (access(mappingPath.c_str(), F_OK) == 0);

//     if (is_critical) {
//         return 0;  // Don't allow mode changes for critical files
//     }

//     int res = chmod(fpath, mode);
//     if (res == -1) {
//         return -errno;
//     }
//     return 0;
// }

// static int criticalfs_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi) {
//     (void) fi;
//     char fpath[PATH_MAX];
//     fullpath(fpath, path);

//     // Check if this is a critical file
//     std::string mappingPath = std::string(fpath) + ".mapping";
//     bool is_critical = (access(mappingPath.c_str(), F_OK) == 0);

//     if (is_critical) {
//         return 0;  // Don't allow ownership changes for critical files
//     }

//     int res = chown(fpath, uid, gid);
//     if (res == -1) {
//         return -errno;
//     }
//     return 0;
// }

static const struct fuse_operations criticalfs_oper = {
    .getattr     = criticalfs_getattr,
    // .readlink    = ...,
    // .mknod       = ...,
    .mkdir       = criticalfs_mkdir,
    .unlink      = criticalfs_unlink,
    .rmdir       = criticalfs_rmdir,
    // .symlink     = ...,
    .rename      = criticalfs_rename,
    // .link        = ...,
    // .chmod       = criticalfs_chmod,
    // .chown       = criticalfs_chown,
    .truncate    = criticalfs_truncate,
    .open        = criticalfs_open,
    .read        = criticalfs_read,
    .write       = criticalfs_write,
    // .statfs      = ...,
    .flush       = criticalfs_flush,
    .release     = criticalfs_release,
    // .fsync       = ...,
    // ... xattr functions ...
    // .opendir     = ...,
    .readdir     = criticalfs_readdir,
    // .releasedir  = ...,
    // .fsyncdir    = ...,
    // .init        = ...,
    // .destroy     = ...,
    // .access      = ...,
    .create      = criticalfs_create,
    // ... other fields ...
};

int main(int argc, char *argv[]) {
    // Setup backing directory
    if (realpath(BACKING_DIR_REL, backing_dir_abs) == NULL) {
        if (mkdir(BACKING_DIR_REL, 0755) == 0) {
            fprintf(stderr, "Created backing directory: %s\n", BACKING_DIR_REL);
            if (realpath(BACKING_DIR_REL, backing_dir_abs) == NULL) {
                perror("realpath after mkdir failed");
                return 1;
            }
        } else {
            perror("mkdir failed");
            return 1;
        }
    }

    struct stat st;
    if (stat(backing_dir_abs, &st) == -1 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Error: '%s' is not a directory\n", backing_dir_abs);
        return 1;
    }

    fprintf(stderr, "Using backing directory: %s\n", backing_dir_abs);
    
    // For now, use the original arguments without modification
    // To enable larger file support, mount with: 
    // ./CriticalFUSE -o max_write=20971520 -o max_read=20971520 -f -d ./mnt
    return fuse_main(argc, argv, &criticalfs_oper, NULL);
} 