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
#include <stddef.h> // For offsetof

// --- Configuration ---
// Consider making this an absolute path or configurable via command line/env var
// For simplicity now, ensure you run the executable from the directory
// containing the 'storage' subdirectory.
#define BACKING_DIR_REL "./storage"
// --- End Configuration ---

// Global variable to store the absolute path to the backing directory
static char backing_dir_abs[PATH_MAX];

// Helper to construct the full path in the backing directory
static void fullpath(char fpath[PATH_MAX], const char *path) {
    // Note: path starts with "/", backing_dir_abs does not end with "/"
    // unless it's the root "/". Handle the root case carefully.
    if (strcmp(path, "/") == 0) {
        snprintf(fpath, PATH_MAX, "%s", backing_dir_abs);
    } else {
        snprintf(fpath, PATH_MAX, "%s%s", backing_dir_abs, path);
    }
    // Optional: Add logging here to see the generated paths
    // fprintf(stderr, "fullpath: input='%s', output='%s'\n", path, fpath);
}

// Get file attributes
static int persistent_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi; // Unused parameter
    char fpath[PATH_MAX];
    fullpath(fpath, path);

    // Use lstat to handle symbolic links correctly
    int res = lstat(fpath, stbuf);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

// Read directory contents
static int persistent_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                              off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    (void) offset; // Unused parameter
    (void) fi;     // Unused parameter
    (void) flags;  // Unused parameter

    char fpath[PATH_MAX];
    fullpath(fpath, path);

    DIR *dp = opendir(fpath);
    if (!dp) {
        return -errno;
    }

    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        // This sets only the file type bits (e.g., S_IFDIR, S_IFREG)
        // A more robust version would lstat each entry here, but this is faster.
        st.st_mode = de->d_type << 12;
        if (filler(buf, de->d_name, &st, 0, 0)) {
            break; // Buffer full
        }
    }
    closedir(dp);
    return 0;
}

// Open a file
static int persistent_open(const char *path, struct fuse_file_info *fi) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);

    // Use the flags provided by FUSE (e.g., O_RDONLY, O_WRONLY, O_RDWR)
    int fd = open(fpath, fi->flags);
    if (fd == -1) {
        return -errno;
    }

    // Store the file descriptor in fuse_file_info for later use
    fi->fh = fd;
    return 0; // Success
}

// Create and open a file
static int persistent_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);

    // Use flags + O_CREAT | O_TRUNC (typical create behavior) and provided mode
    // Note: FUSE might have already added O_CREAT etc. to fi->flags depending on version/call
    // Using open directly with specific flags is often clearer for create.
    // Let's trust fi->flags includes O_CREAT as passed by the kernel VFS layer.
    int fd = open(fpath, fi->flags, mode);
    if (fd == -1) {
        return -errno;
    }

    // Store the file descriptor
    fi->fh = fd;
    return 0; // Success
}


// Read data from an open file
static int persistent_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) path; // We use fi->fh, not the path directly

    // Use the file descriptor stored in fi->fh
    int fd = fi->fh;
    int res = pread(fd, buf, size, offset);
    if (res == -1) {
        return -errno;
    }
    return res; // Return number of bytes read
}

// Write data to an open file
static int persistent_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) path; // We use fi->fh, not the path directly

    // Use the file descriptor stored in fi->fh
    int fd = fi->fh;
    int res = pwrite(fd, buf, size, offset);
    if (res == -1) {
        return -errno;
    }
    return res; // Return number of bytes written
}

// Release (close) an open file
static int persistent_release(const char *path, struct fuse_file_info *fi) {
    (void) path; // Unused parameter

    // Close the file descriptor stored in fi->fh
    close(fi->fh);
    return 0;
}

// Truncate a file
static int persistent_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);

    int res;
    // If fi is not NULL, the file might be open. Use ftruncate if possible.
    // However, the VFS layer might call truncate even if the file isn't open
    // in *this* FUSE process context, so just using truncate is safer/simpler.
    // if (fi != NULL) {
    //     res = ftruncate(fi->fh, size);
    // } else {
        res = truncate(fpath, size);
    // }

    if (res == -1)
        return -errno;

    return 0;
}

// --- Add other necessary operations ---

// Create a directory
static int persistent_mkdir(const char *path, mode_t mode) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    int res = mkdir(fpath, mode);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

// Remove a file
static int persistent_unlink(const char *path) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    int res = unlink(fpath);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

// Remove a directory
static int persistent_rmdir(const char *path) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    int res = rmdir(fpath);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

// Rename a file or directory
static int persistent_rename(const char *from, const char *to, unsigned int flags) {
	// Note: flags parameter was added in FUSE 2.9, default is 0 if not supported
	// flags can be RENAME_NOREPLACE, RENAME_EXCHANGE
	#ifdef RENAME_NOREPLACE // Use ifdef for older libfuse compatibility
	if (flags & RENAME_NOREPLACE || flags & RENAME_EXCHANGE) {
		// Handle flags if needed, otherwise return -EINVAL or ignore
        // For this simple example, we might ignore them or return error
        if (flags != 0) return -EINVAL; // Simplest: reject flags
	}
	#else
    // On older systems without flags parameter, ensure flags is 0 if passed somehow
    if (flags != 0) return -EINVAL;
    #endif


    char fpath_from[PATH_MAX];
    char fpath_to[PATH_MAX];
    fullpath(fpath_from, from);
    fullpath(fpath_to, to);

    int res = rename(fpath_from, fpath_to);
    if (res == -1) {
        return -errno;
    }
    return 0;
}


// Define the FUSE operations structure
// Use offsetof to initialize only the implemented functions correctly
static const struct fuse_operations persistent_oper = {
    .getattr    = persistent_getattr,
    .readdir    = persistent_readdir,
    .open       = persistent_open,
    .create     = persistent_create,
    .read       = persistent_read,
    .write      = persistent_write,
    .release    = persistent_release, // Added release
    .truncate   = persistent_truncate,
    .mkdir      = persistent_mkdir,   // Added mkdir
    .unlink     = persistent_unlink,  // Added unlink (rm file)
    .rmdir      = persistent_rmdir,   // Added rmdir (rm dir)
    .rename     = persistent_rename,  // Added rename
    // Add other operations like symlink, link, chmod, chown etc. as needed
};

int main(int argc, char *argv[]) {
    // --- Path Setup ---
    // Get the absolute path of the backing directory relative to cwd
    // This makes the FUSE mount independent of where you start it from,
    // as long as the 'storage' dir exists relative to the executable's CWD
    // at the time of launch.
    // A more robust solution takes the backing path as a command-line argument.
    if (realpath(BACKING_DIR_REL, backing_dir_abs) == NULL) {
         // If realpath fails, maybe the dir doesn't exist yet. Try creating it.
         if (mkdir(BACKING_DIR_REL, 0755) == 0) {
             fprintf(stderr, "Created backing directory: %s\n", BACKING_DIR_REL);
             // Try realpath again
             if (realpath(BACKING_DIR_REL, backing_dir_abs) == NULL) {
                 perror("realpath after mkdir failed");
                 fprintf(stderr, "Error: Could not determine absolute path for backing directory '%s' even after creating it.\n", BACKING_DIR_REL);
                 return 1;
             }
         } else if (errno == EEXIST) {
             // Directory exists, try realpath again (maybe permissions issue before?)
             if (realpath(BACKING_DIR_REL, backing_dir_abs) == NULL) {
                 perror("realpath on existing dir failed");
                 fprintf(stderr, "Error: Could not determine absolute path for existing backing directory '%s'. Check permissions.\n", BACKING_DIR_REL);
                 return 1;
             }
             // If it exists, we continue, assuming it's a directory.
             // A stat check could be added here for more robustness.
         }
          else {
             // mkdir failed for another reason
             perror("mkdir failed");
             fprintf(stderr, "Error: Could not create or find backing directory: %s\n", BACKING_DIR_REL);
             return 1;
         }
    }
    fprintf(stderr, "Using backing directory: %s\n", backing_dir_abs);

    // Ensure it's actually a directory (optional but good practice)
    struct stat st;
    if (stat(backing_dir_abs, &st) == -1) {
        perror("stat on backing directory failed");
        fprintf(stderr,"Error: Cannot stat backing directory '%s'.\n", backing_dir_abs);
        return 1;
    }
    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr,"Error: Backing path '%s' is not a directory.\n", backing_dir_abs);
        return 1;
    }
    // --- End Path Setup ---


    // Pass arguments directly to fuse_main
    // fuse_main will handle FUSE options like -f (foreground), -d (debug), mountpoint
    return fuse_main(argc, argv, &persistent_oper, NULL);
}