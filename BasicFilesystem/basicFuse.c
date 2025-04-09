#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>

#define BACKING_DIR "./storage"

static void fullpath(char fpath[PATH_MAX], const char *path) {
    snprintf(fpath, PATH_MAX, "%s%s", BACKING_DIR, path);
}

static int persistent_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    return lstat(fpath, stbuf) == -1 ? -errno : 0;
}

static int persistent_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                              off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    DIR *dp = opendir(fpath);
    if (!dp) return -errno;

    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;
        filler(buf, de->d_name, &st, 0, 0);
    }
    closedir(dp);
    return 0;
}

static int persistent_open(const char *path, struct fuse_file_info *fi) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    int fd = open(fpath, fi->flags);
    if (fd == -1) return -errno;
    close(fd);
    return 0;
}

static int persistent_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    int fd = open(fpath, fi->flags | O_CREAT, mode);
    if (fd == -1) return -errno;
    close(fd);
    return 0;
}

static int persistent_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    int fd = open(fpath, O_RDONLY);
    if (fd == -1) return -errno;
    int res = pread(fd, buf, size, offset);
    close(fd);
    return res == -1 ? -errno : res;
}

static int persistent_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    int fd = open(fpath, O_WRONLY);
    if (fd == -1) return -errno;
    int res = pwrite(fd, buf, size, offset);
    close(fd);
    return res == -1 ? -errno : res;
}

static const struct fuse_operations persistent_oper = {
    .getattr = persistent_getattr,
    .readdir = persistent_readdir,
    .open    = persistent_open,
    .read    = persistent_read,
    .write   = persistent_write,
    .create  = persistent_create,
};

int main(int argc, char *argv[]) {
    // Ensure storage directory exists
    mkdir(BACKING_DIR, 0755);
    return fuse_main(argc, argv, &persistent_oper, NULL);
}
