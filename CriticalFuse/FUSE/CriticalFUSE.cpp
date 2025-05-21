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

#include "../FileHandlers/AbstractFile.h"
#include "../FileHandlers/TextFile.h"
#include "../FileHandlers/RawFile.h"
#include "../FileHandlers/DngFile.h"

#define BACKING_DIR_REL "./storage"
static char backing_dir_abs[PATH_MAX];

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
    if (ext_lower == "dng") {
        return std::make_unique<DngFileHandler>();
    }
    // Unsupported file type, treat as regular file
    return nullptr;
}

// FUSE operations
static int criticalfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi;
    char fpath[PATH_MAX];
    fullpath(fpath, path);

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
    return 0;
}

static int criticalfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) fi;
    char fpath[PATH_MAX];
    fullpath(fpath, path);

    // Check if this is a critical file
    std::string mappingPath = std::string(fpath) + ".mapping";
    if (access(mappingPath.c_str(), F_OK) == 0) {
        auto handler = getFileHandler(path);
        if (!handler) {
            // Not a supported file type, treat as regular file
            return -ENOENT;
        }
        if (handler->readFile(mappingPath.c_str(), buf, size, offset) != ResultCode::SUCCESS) {
            return -errno;
        }
        return size;
    }

    // Not a critical file, read directly
    int fd = open(fpath, O_RDONLY);
    if (fd == -1) {
        return -errno;
    }

    int res = pread(fd, buf, size, offset);
    close(fd);
    if (res == -1) {
        return -errno;
    }
    return res;
}

static int criticalfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) fi;
    char fpath[PATH_MAX];
    fullpath(fpath, path);

    // Check if this is a critical file
    std::string mappingPath = std::string(fpath) + ".mapping";
    if (access(mappingPath.c_str(), F_OK) == 0) {
        auto handler = getFileHandler(path);
        if (!handler) {
            // Not a supported file type, treat as regular file
            return -ENOENT;
        }
        if (handler->writeFile(mappingPath.c_str(), buf, size, offset) != ResultCode::SUCCESS) {
            return -errno;
        }
        std::cout << "Wrote " << size << " bytes to file: " << path << std::endl;
        std::cout << "Offset: " << offset << std::endl;
        std::cout << "Buffer: " << buf << std::endl;
        std::cout << "Mapping path: " << mappingPath << std::endl;
        std::cout << "File path: " << fpath << std::endl;

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

static int criticalfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);

    // Only create mapping for supported file types
    auto handler = getFileHandler(path);
    if (handler) {
        std::string mappingPath = std::string(fpath) + ".mapping";
        if (handler->createMapping("", 0) != ResultCode::SUCCESS) {
            unlink(fpath); // Clean up the created file
            return -errno;
        }
        if (handler->saveMapToFile(mappingPath.c_str()) != ResultCode::SUCCESS) {
            unlink(fpath); // Clean up the created file
            return -errno;
        }
        std::cout << "Created mapping file: " << mappingPath << std::endl;

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
    // .chmod       = ...,
    // .chown       = ...,
    // .truncate    = ...,
    .open        = criticalfs_open,
    .read        = criticalfs_read,
    .write       = criticalfs_write,
    // .statfs      = ...,
    // .flush       = ...,
    // .release     = criticalfs_release, // Added release to match open
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
    return fuse_main(argc, argv, &criticalfs_oper, NULL);
} 