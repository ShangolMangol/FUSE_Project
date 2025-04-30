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

// --- FUSE Operations ---
static int splitfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi;
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
    char part0[PATH_MAX];
    get_part_paths(path, part0, NULL);

    int fd = open(part0, fi->flags | O_CREAT, mode);
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

    struct stat st;
    if (stat(fpath0, &st) == -1) return -errno;
    off_t split_point = st.st_size;   // split_point = size0

    int fd;
    ssize_t res;

    // Check if the offset is within the first part or if the first part is empty
    // If offset < split_point, write to part0; else write to part1
    if (offset < split_point || split_point == 0) {
        fd = open(fpath0, O_WRONLY);
        if (fd == -1) return -errno;
        res = pwrite(fd, buf, size, offset);
        close(fd);
        if (res == -1) return -errno;
    } else {
        fd = open(fpath1, O_WRONLY | O_CREAT, 0644);
        if (fd == -1) return -errno;
        res = pwrite(fd, buf, size, offset - split_point);
        close(fd);
        if (res == -1) return -errno;
    }

    return res;
}


static int splitfs_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    char fpath0[PATH_MAX], fpath1[PATH_MAX];
    snprintf(fpath0, PATH_MAX, "%s%s.part0", backing_dir_abs, path);
    snprintf(fpath1, PATH_MAX, "%s%s.part1", backing_dir_abs, path);

    struct stat st0, st1;
    if (stat(fpath0, &st0) == -1) return -errno;
    if (stat(fpath1, &st1) == -1) return -errno;

    off_t size0 = st0.st_size;
    off_t size1 = st1.st_size;

    off_t total_size = size0 + size1;
    off_t split_point = size / 2 + (size % 2 != 0); // logical split point

    // If truncating below the split point, adjust only part0
    if (size < size0) {
        if (truncate(fpath0, split_point) == -1) return -errno;
        if (truncate(fpath1, 0) == -1) return -errno;
    }
    // If truncating between parts, adjust both parts accordingly
    else if (size < total_size) {
        if (truncate(fpath0, split_point) == -1) return -errno;
        if (truncate(fpath1, size - split_point) == -1) return -errno;
    }
    // If truncating beyond both parts, extend both parts
    else {
        // Extend part0
        if (truncate(fpath0, split_point) == -1) return -errno;
        // Extend part1
        if (truncate(fpath1, size - split_point) == -1) return -errno;
    }

    return 0;
}


static int splitfs_unlink(const char *path) {
    char part0[PATH_MAX], part1[PATH_MAX];
    get_part_paths(path, part0, part1);
    unlink(part0);
    unlink(part1);
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
