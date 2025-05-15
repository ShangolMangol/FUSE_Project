#define FUSE_USE_VERSION 31
#define _GNU_SOURCE

#include <fuse3/fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <limits.h>
#include <search.h>  // For hcreate, hsearch, hdestroy

#define BACKING_DIR_REL "./storage"
static char backing_dir_abs[PATH_MAX];

// --- Utility: Generate paths for both parts ---
static void fullpath(char fpath[PATH_MAX], const char *path) {
    /* Params:
    fpath - output buffer, where the final absolute path will be written.
    path - the path to add to the relative FUSE path

    Output:
    the absolute path of a file at path in the FUSE, based on the relative FUSE path 
     */ 
    snprintf(fpath, PATH_MAX, "%s%s", backing_dir_abs, path);
}

static void get_part_paths(const char *path, char part0[PATH_MAX], char part1[PATH_MAX]) {
    char base[PATH_MAX];
    fullpath(base, path);
    snprintf(part0, PATH_MAX, "%s.part0", base);
    snprintf(part1, PATH_MAX, "%s.part1", base);
}

static int str_ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) return 0;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    return lenstr >= lensuffix && strcmp(str + lenstr - lensuffix, suffix) == 0;
}

// Utility function for copy-on-write operations
static int copy_file_contents(const char *src_path, const char *dst_path) {
    if (access(src_path, F_OK) != 0) {
        // Source doesn't exist, create empty destination
        int fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) return -errno;
        close(fd);
        return 0;
    }
    
    int fd_src = open(src_path, O_RDONLY);
    if (fd_src == -1) return -errno;
    
    int fd_dst = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_dst == -1) {
        close(fd_src);
        return -errno;
    }
    
    char buffer[8192];
    ssize_t bytes_read;
    
    while ((bytes_read = read(fd_src, buffer, sizeof(buffer))) > 0) {
        if (write(fd_dst, buffer, bytes_read) != bytes_read) {
            close(fd_src);
            close(fd_dst);
            unlink(dst_path);
            return -errno;
        }
    }
    
    close(fd_src);
    close(fd_dst);
    
    return 0;
}

// Merge two files into one temporary file
static int merge_files(const char *part0, const char *part1, const char *merged_path) {
    // Open or create the destination file
    int fd_dst = open(merged_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_dst == -1) return -errno;
    
    // Copy part0 if it exists
    if (access(part0, F_OK) == 0) {
        int fd_src = open(part0, O_RDONLY);
        if (fd_src == -1) {
            close(fd_dst);
            return -errno;
        }
        
        char buffer[8192];
        ssize_t bytes_read;
        
        while ((bytes_read = read(fd_src, buffer, sizeof(buffer))) > 0) {
            if (write(fd_dst, buffer, bytes_read) != bytes_read) {
                close(fd_src);
                close(fd_dst);
                unlink(merged_path);
                return -errno;
            }
        }
        
        close(fd_src);
    }
    
    // Append part1 if it exists
    if (access(part1, F_OK) == 0) {
        int fd_src = open(part1, O_RDONLY);
        if (fd_src == -1) {
            close(fd_dst);
            return -errno;
        }
        
        char buffer[8192];
        ssize_t bytes_read;
        
        while ((bytes_read = read(fd_src, buffer, sizeof(buffer))) > 0) {
            if (write(fd_dst, buffer, bytes_read) != bytes_read) {
                close(fd_src);
                close(fd_dst);
                unlink(merged_path);
                return -errno;
            }
        }
        
        close(fd_src);
    }
    
    close(fd_dst);
    return 0;
}

// Split a file into two parts at the middle
static int split_file(const char *merged_path, const char *part0, const char *part1) {
    struct stat st;
    if (stat(merged_path, &st) == -1) return -errno;
    
    off_t total_size = st.st_size;
    off_t mid_point = total_size / 2;
    if (total_size % 2 != 0) mid_point++; // if odd, slightly favor part0
    
    // Open the merged file
    int fd_src = open(merged_path, O_RDONLY);
    if (fd_src == -1) return -errno;
    
    // Create part0
    int fd_dst0 = open(part0, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_dst0 == -1) {
        close(fd_src);
        return -errno;
    }
    
    // Copy first half to part0
    char buffer[8192];
    off_t bytes_copied = 0;
    ssize_t bytes_read;
    
    while (bytes_copied < mid_point && (bytes_read = read(fd_src, buffer, 
            (sizeof(buffer) < (mid_point - bytes_copied)) ? sizeof(buffer) : (mid_point - bytes_copied))) > 0) {
        
        if (write(fd_dst0, buffer, bytes_read) != bytes_read) {
            close(fd_src);
            close(fd_dst0);
            unlink(part0);
            return -errno;
        }
        
        bytes_copied += bytes_read;
    }
    
    close(fd_dst0);
    
    // Create part1
    int fd_dst1 = open(part1, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_dst1 == -1) {
        close(fd_src);
        return -errno;
    }
    
    // Copy second half to part1
    while ((bytes_read = read(fd_src, buffer, sizeof(buffer))) > 0) {
        if (write(fd_dst1, buffer, bytes_read) != bytes_read) {
            close(fd_src);
            close(fd_dst1);
            unlink(part1);
            return -errno;
        }
    }
    
    close(fd_src);
    close(fd_dst1);
    
    return 0;
}

// --- FUSE Operations ---
static int splitfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi;

    char fullpath[PATH_MAX];
    snprintf(fullpath, PATH_MAX, "%s%s", backing_dir_abs, path);

    // First: handle directories (or any unsplit files, like control files)
    if (stat(fullpath, stbuf) == 0) {
        return 0;  // Found actual file or directory
    }

    // Fallback: check for .part0 and .part1 (split files)
    char part0[PATH_MAX], part1[PATH_MAX];
    get_part_paths(path, part0, part1);

    struct stat st0, st1;
    int res0 = stat(part0, &st0);
    int res1 = stat(part1, &st1);

    if (res0 == -1 && res1 == -1) return -ENOENT;

    memset(stbuf, 0, sizeof(struct stat));
    stbuf->st_mode = S_IFREG | 0644;
    stbuf->st_nlink = 1;
    stbuf->st_size = 0;

    if (res0 == 0) stbuf->st_size += st0.st_size;
    if (res1 == 0) stbuf->st_size += st1.st_size;

    return 0;
}

static int splitfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                              off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    (void) offset;
    (void) fi;
    (void) flags;

    char fpath[PATH_MAX];
    fullpath(fpath, path);

    DIR *dp = opendir(fpath);
    if (!dp) return -errno;

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    // Create a small hash table to track seen base names
    hcreate(128);  // number of expected files

    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        const char *name = de->d_name;

        // Skip system entries
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

        const char *suffix = NULL;
        if (str_ends_with(name, ".part0")) {
            suffix = ".part0";
        } else if (str_ends_with(name, ".part1")) {
            suffix = ".part1";
        } else {
            continue;  // Not a split file part; skip
        }

        // Strip suffix to get base name
        size_t base_len = strlen(name) - strlen(suffix);
        char base_name[NAME_MAX];
        strncpy(base_name, name, base_len);
        base_name[base_len] = '\0';

        // Check hash table for duplicates
        ENTRY e, *ep;
        e.key = base_name;
        e.data = (void *)1;
        ep = hsearch(e, FIND);
        if (!ep) {
            // Not seen before; add it and report to user
            filler(buf, base_name, NULL, 0, 0);
            e.key = strdup(base_name);  // strdup needed because hsearch keeps pointers
            hsearch(e, ENTER);
        }
    }

    closedir(dp);
    hdestroy();  // Free hash table (not keys — small leak acceptable for FUSE context)
    return 0;
}


static int splitfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    char part0[PATH_MAX], part1[PATH_MAX];
    get_part_paths(path, part0, part1);
    
    char temp_merged[PATH_MAX], temp0[PATH_MAX], temp1[PATH_MAX];
    snprintf(temp_merged, PATH_MAX, "%s%s.merged.tmp", backing_dir_abs, path);
    snprintf(temp0, PATH_MAX, "%s%s.part0.tmp", backing_dir_abs, path);
    snprintf(temp1, PATH_MAX, "%s%s.part1.tmp", backing_dir_abs, path);

    // Create a temporary merged file
    int fd = open(temp_merged, fi->flags | O_CREAT, mode);
    if (fd == -1) return -errno;
    close(fd);
    
    // Split the (empty) merged file into two parts
    int res = split_file(temp_merged, temp0, temp1);
    unlink(temp_merged); // Clean up merged file
    
    if (res < 0) {
        return res;
    }
    
    // Atomically rename temporary files to their final destinations
    if (rename(temp0, part0) == -1) {
        unlink(temp0);
        unlink(temp1);
        return -errno;
    }
    
    if (rename(temp1, part1) == -1) {
        unlink(temp1);
        return -errno;
    }

    // We still need to open the file for the caller
    fd = open(part0, fi->flags, mode);
    if (fd == -1) return -errno;
    
    fi->fh = fd;
    return 0;
}

static int splitfs_open(const char *path, struct fuse_file_info *fi) {
    // We don't use fi->fh here due to split storage
    return 0;
}

static int splitfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    char part0[PATH_MAX], part1[PATH_MAX];
    get_part_paths(path, part0, part1);

    struct stat st0 = {0}, st1 = {0};
    off_t size0 = 0, size1 = 0;
    stat(part0, &st0); stat(part1, &st1);
    size0 = st0.st_size; size1 = st1.st_size;

    if (offset >= size0 + size1) return 0;

    size_t total_read = 0;

    if (offset < size0) {
        int fd0 = open(part0, O_RDONLY);
        if (fd0 == -1) return -errno;

        size_t to_read = (offset + size > size0) ? size0 - offset : size;
        ssize_t r = pread(fd0, buf, to_read, offset);
        close(fd0);
        if (r == -1) return -errno;
        total_read += r;
    }

    if (offset + size > size0) {
        int fd1 = open(part1, O_RDONLY);
        if (fd1 == -1) return -errno;

        off_t off1 = offset > size0 ? offset - size0 : 0;
        size_t to_read = size - total_read;
        ssize_t r = pread(fd1, buf + total_read, to_read, off1);
        close(fd1);
        if (r == -1) return -errno;
        total_read += r;
    }

    return total_read;
}


static int splitfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) fi;

    char fpath0[PATH_MAX], fpath1[PATH_MAX];
    snprintf(fpath0, PATH_MAX, "%s%s.part0", backing_dir_abs, path);
    snprintf(fpath1, PATH_MAX, "%s%s.part1", backing_dir_abs, path);

    char temp0[PATH_MAX], temp1[PATH_MAX], temp_merged[PATH_MAX];
    snprintf(temp0, PATH_MAX, "%s%s.part0.tmp", backing_dir_abs, path);
    snprintf(temp1, PATH_MAX, "%s%s.part1.tmp", backing_dir_abs, path);
    snprintf(temp_merged, PATH_MAX, "%s%s.merged.tmp", backing_dir_abs, path);

    // Merge the two parts into a single temporary file
    int res = merge_files(fpath0, fpath1, temp_merged);
    if (res < 0) return res;
    
    // Write to the merged file
    int fd = open(temp_merged, O_WRONLY);
    if (fd == -1) {
        unlink(temp_merged);
        return -errno;
    }
    
    ssize_t write_res = pwrite(fd, buf, size, offset);
    close(fd);
    
    if (write_res == -1) {
        unlink(temp_merged);
        return -errno;
    }
    
    // Split the merged file back into two parts
    res = split_file(temp_merged, temp0, temp1);
    unlink(temp_merged); // Clean up merged file
    
    if (res < 0) {
        return res;
    }
    
    // Atomic rename to replace original files
    if (rename(temp0, fpath0) == -1) {
        unlink(temp0);
        unlink(temp1);
        return -errno;
    }
    
    if (rename(temp1, fpath1) == -1) {
        unlink(temp1);
        return -errno;
    }

    return write_res;
}

static int splitfs_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    (void) fi; // fi is not used as we operate directly on backing files.

    char fpath0[PATH_MAX], fpath1[PATH_MAX];
    get_part_paths(path, fpath0, fpath1);
    
    char temp0[PATH_MAX], temp1[PATH_MAX], temp_merged[PATH_MAX];
    snprintf(temp0, PATH_MAX, "%s%s.part0.tmp", backing_dir_abs, path);
    snprintf(temp1, PATH_MAX, "%s%s.part1.tmp", backing_dir_abs, path);
    snprintf(temp_merged, PATH_MAX, "%s%s.merged.tmp", backing_dir_abs, path);

    // Merge the two parts into a single temporary file
    int res = merge_files(fpath0, fpath1, temp_merged);
    if (res < 0) return res;
    
    // Truncate the merged file
    res = truncate(temp_merged, size);
    if (res < 0) {
        unlink(temp_merged);
        return -errno;
    }
    
    // Split the merged file back into two parts
    res = split_file(temp_merged, temp0, temp1);
    unlink(temp_merged); // Clean up merged file
    
    if (res < 0) {
        return res;
    }
    
    // Atomic rename to replace original files
    if (rename(temp0, fpath0) == -1) {
        unlink(temp0);
        unlink(temp1);
        return -errno;
    }
    
    if (rename(temp1, fpath1) == -1) {
        unlink(temp1);
        return -errno;
    }

    return 0; // Success
}

static int splitfs_unlink(const char *path) {
    char part0[PATH_MAX], part1[PATH_MAX];
    get_part_paths(path, part0, part1);
    
    int res0 = unlink(part0);
    int res1 = unlink(part1);
    
    // If both failed, return error from part0 (most likely to exist)
    if (res0 == -1 && res1 == -1) {
        // Only return error if at least one of the parts should have existed
        if (access(part0, F_OK) == 0 || access(part1, F_OK) == 0) {
            return -errno;
        }
    }
    
    return 0;
}

static int splitfs_mkdir(const char *path, mode_t mode) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    return mkdir(fpath, mode) == -1 ? -errno : 0;
}

static int splitfs_rmdir(const char *path) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    return rmdir(fpath) == -1 ? -errno : 0;
}

static int splitfs_rename(const char *from, const char *to, unsigned int flags) {
    if (flags) return -EINVAL;

    char from0[PATH_MAX], from1[PATH_MAX], to0[PATH_MAX], to1[PATH_MAX];
    get_part_paths(from, from0, from1);
    get_part_paths(to, to0, to1);

    if (rename(from0, to0) == -1 && errno != ENOENT) return -errno;
    if (rename(from1, to1) == -1 && errno != ENOENT) return -errno;
    return 0;
}

// --- Main ---

static const struct fuse_operations splitfs_oper = {
    .getattr = splitfs_getattr,
    .readdir = splitfs_readdir,
    .open = splitfs_open,
    .create = splitfs_create,
    .read = splitfs_read,
    .write = splitfs_write,
    .truncate = splitfs_truncate,
    .unlink = splitfs_unlink,
    .mkdir = splitfs_mkdir,
    .rmdir = splitfs_rmdir,
    .rename = splitfs_rename,
};

int main(int argc, char *argv[]) {
    if (realpath(BACKING_DIR_REL, backing_dir_abs) == NULL) {
        mkdir(BACKING_DIR_REL, 0755);
        if (realpath(BACKING_DIR_REL, backing_dir_abs) == NULL) {
            perror("realpath failed");
            return 1;
        }
    }

    struct stat st;
    if (stat(backing_dir_abs, &st) == -1 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Error: '%s' is not a directory\n", backing_dir_abs);
        return 1;
    }

    return fuse_main(argc, argv, &splitfs_oper, NULL);
}
