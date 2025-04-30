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
#include <limits.h> // For PATH_MAX
#include <stdbool.h>
#include <libgen.h> // For dirname, basename (might need careful handling)
#include <sys/types.h>
#include <ctype.h> // For isdigit


// --- Configuration ---
#define BACKING_DIR_REL "./storage"
// Define the size of each file part (e.g., 4MB)
#define CHUNK_SIZE (4 * 1024 * 1024)
// Suffix for part files
#define PART_SUFFIX ".part"
// --- End Configuration ---

// Global variable to store the absolute path to the backing directory
static char backing_dir_abs[PATH_MAX];

// --- Helper Functions ---

// Construct the full path in the backing directory
static void fullpath(char fpath[PATH_MAX], const char *path) {
    if (strcmp(path, "/") == 0) {
        snprintf(fpath, PATH_MAX, "%s", backing_dir_abs);
    } else {
        snprintf(fpath, PATH_MAX, "%s%s", backing_dir_abs, path);
    }
    // fprintf(stderr, "DEBUG: fullpath: input='%s', output='%s'\n", path, fpath);
}

// Construct the path for a specific part file
static void get_part_path(char part_fpath[PATH_MAX], const char *base_fpath, int part_idx) {
    snprintf(part_fpath, PATH_MAX, "%s%s%d", base_fpath, PART_SUFFIX, part_idx);
}

// Check if a filename looks like a part file and return the base name and part number
static bool parse_part_filename(const char *filename, char *base_name_buf, size_t buf_size, int *part_idx) {
    const char *part_suffix_pos = strstr(filename, PART_SUFFIX);
    if (!part_suffix_pos) return false;

    size_t base_len = part_suffix_pos - filename;
    if (base_len == 0 || base_len >= buf_size) return false; // Invalid base name or buffer too small

    // Check if characters after suffix are digits
    const char *num_start = part_suffix_pos + strlen(PART_SUFFIX);
    if (*num_start == '\0') return false; // No number after suffix

    char *endptr;
    long val = strtol(num_start, &endptr, 10);

    // Check if the entire rest of the string was consumed and is a valid integer
    if (*endptr != '\0' || val < 0 || val > INT_MAX) return false;

    // Copy base name
    strncpy(base_name_buf, filename, base_len);
    base_name_buf[base_len] = '\0';

    *part_idx = (int)val;
    return true;
}

// Get stats for a split file (total size, mode from part 0)
// Returns 0 on success, -errno on failure
static int get_split_file_stats(const char *fpath, struct stat *stbuf) {
    char part0_path[PATH_MAX];
    get_part_path(part0_path, fpath, 0);

    // We need stats from part 0 for mode, timestamps etc.
    if (lstat(part0_path, stbuf) == -1) {
        // If part 0 doesn't exist, the logical file doesn't exist
        return -errno; // Likely ENOENT
    }

    // Now calculate total size
    off_t total_size = 0;
    int part_idx = 0;
    struct stat part_st;
    char current_part_path[PATH_MAX];

    while (true) {
        get_part_path(current_part_path, fpath, part_idx);
        if (lstat(current_part_path, &part_st) == -1) {
            if (errno == ENOENT) {
                // We found the end
                if (part_idx == 0) {
                    // Part 0 didn't exist after all? Should have been caught above.
                    // Or maybe it existed but was deleted between checks (race condition).
                    // Or maybe it's an empty file represented only by part0 of size 0?
                    total_size = 0; // Assume empty file
                } else {
                    // Size is determined by previous parts
                }
                break; // Stop calculation
            } else {
                // Error accessing a part file
                fprintf(stderr, "ERROR: lstat failed for %s: %s\n", current_part_path, strerror(errno));
                return -errno;
            }
        }

        // Check if it's a regular file as expected
        if (!S_ISREG(part_st.st_mode)) {
             fprintf(stderr, "ERROR: Part file %s is not a regular file!\n", current_part_path);
             return -EIO; // Indicate an internal inconsistency
        }

        // Logic: Add CHUNK_SIZE for all *but* the last part found.
        // So, we tentatively add the size, and if a *next* part exists,
        // we adjust the previous addition to CHUNK_SIZE.
        // Let's try a simpler approach: Find the highest index first.

        part_idx++; // Check next part
    }

    // Now part_idx is one greater than the highest index found
    int max_part_idx = part_idx - 1;

    if (max_part_idx < 0) {
        // No parts found (part 0 check must have failed or file is truly empty)
        total_size = 0;
    } else {
        // Size = (N * CHUNK_SIZE) + size_of_last_part
        if (max_part_idx > 0) {
            total_size = (off_t)max_part_idx * CHUNK_SIZE;
        }
        // Get size of the actual last part
        get_part_path(current_part_path, fpath, max_part_idx);
        if (lstat(current_part_path, &part_st) == 0) {
             total_size += part_st.st_size;
             // Sanity check: last part size should not exceed CHUNK_SIZE unless it's part 0
             if (part_st.st_size > CHUNK_SIZE && max_part_idx > 0) {
                 fprintf(stderr, "WARNING: Part %s size (%ld) exceeds CHUNK_SIZE (%d)\n",
                         current_part_path, (long)part_st.st_size, CHUNK_SIZE);
                 // Proceed anyway? Or return error? Let's proceed for now.
             }
        } else {
             // Error getting stats for the last part we know exists? Problem.
             fprintf(stderr, "ERROR: Failed to stat last known part %s: %s\n", current_part_path, strerror(errno));
             return -errno;
        }
    }

    // Update stat buffer with calculated size and ensure it's marked as a regular file
    stbuf->st_size = total_size;
    stbuf->st_mode = (stbuf->st_mode & ~S_IFMT) | S_IFREG; // Ensure S_IFREG
    stbuf->st_blocks = (total_size + 511) / 512; // Estimate blocks
    stbuf->st_nlink = 1; // Logical file has one link

    // fprintf(stderr, "DEBUG: get_split_file_stats for %s: size=%ld, mode=%o\n", fpath, (long)total_size, stbuf->st_mode);
    return 0;
}

// Delete all parts associated with a base path
static int delete_all_parts(const char *fpath) {
    int part_idx = 0;
    char part_path[PATH_MAX];
    int last_errno = 0; // Store the first error encountered

    fprintf(stderr, "DEBUG: delete_all_parts for %s\n", fpath);
    while (true) {
        get_part_path(part_path, fpath, part_idx);
        if (unlink(part_path) == -1) {
            if (errno == ENOENT) {
                // No more parts, successful if part_idx > 0 or if 0 and no prior error
                 if (part_idx == 0 && last_errno == 0) {
                    // If part 0 didn't exist, maybe it was never created or already deleted.
                    // This could happen if unlink is called on a non-existent file.
                    // Check if the base path itself exists as a non-part file.
                    struct stat st;
                    if (lstat(fpath, &st) == 0) {
                       // It exists as something else, let the caller handle it? Or unlink it here?
                       // Let's assume split_unlink checks first. Return ENOENT as if no parts found.
                       return -ENOENT;
                    }
                    // If fpath also doesn't exist, then ENOENT is correct.
                    return -ENOENT;
                 }
                 // Otherwise, we are done deleting.
                 break;
            } else {
                // Error deleting a part
                fprintf(stderr, "ERROR: Failed to unlink part %s: %s\n", part_path, strerror(errno));
                if (last_errno == 0) {
                    last_errno = errno; // Record the first error
                }
                // Continue trying to delete other parts? Yes.
            }
        }
        part_idx++;
    }
    return -last_errno; // Return 0 if successful, or -first_error_code
}


// --- FUSE Operations ---

static int split_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi; // Unused parameter
    char fpath[PATH_MAX];
    fullpath(fpath, path);

    // fprintf(stderr, "DEBUG: split_getattr: path='%s', fpath='%s'\n", path, fpath);

    // Is the requested path itself a part file? Hide these.
    char base_name[PATH_MAX];
    int part_idx;
    // Need basename of fpath to check suffix
    char *fname = basename(strdup(fpath)); // Note: basename might modify input, so use strdup
    if (!fname) return -ENOMEM;
    bool is_part = parse_part_filename(fname, base_name, sizeof(base_name), &part_idx);
    free(fname);
    if (is_part) {
        // fprintf(stderr, "DEBUG: split_getattr: Hiding part file %s\n", fpath);
        return -ENOENT; // Hide part files from direct access
    }

    // Try to get stats assuming it's a split file first
    int res = get_split_file_stats(fpath, stbuf);
    if (res == 0) {
        // Success, it's a split file (or empty file represented by part0 size 0)
        // fprintf(stderr, "DEBUG: split_getattr: Found split file %s, size %ld\n", fpath, (long)stbuf->st_size);
        return 0;
    } else if (res == -ENOENT) {
        // Part 0 doesn't exist. Maybe it's a directory or a regular non-split file?
        // fprintf(stderr, "DEBUG: split_getattr: Part 0 not found for %s, trying lstat\n", fpath);
        res = lstat(fpath, stbuf);
        if (res == -1) {
            // fprintf(stderr, "DEBUG: split_getattr: lstat failed for %s: %s\n", fpath, strerror(errno));
            return -errno;
        }
        // fprintf(stderr, "DEBUG: split_getattr: Found non-split file/dir %s\n", fpath);
        // It exists as something else (e.g., directory). Return its stats.
        return 0;
    } else {
        // Other error occurred during split file stat calculation
        fprintf(stderr, "DEBUG: split_getattr: Error %d getting split stats for %s\n", res, fpath);
        return res;
    }
}

// Custom data structure for readdir to track seen logical names
typedef struct SeenName {
    char *name;
    struct SeenName *next;
} SeenName;

static bool has_been_seen(SeenName **head, const char *name) {
    SeenName *current = *head;
    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            return true;
        }
        current = current->next;
    }
    return false;
}

static void add_seen_name(SeenName **head, const char *name) {
    SeenName *new_node = (SeenName *)malloc(sizeof(SeenName));
    if (!new_node) return; // Allocation failed
    new_node->name = strdup(name);
    if (!new_node->name) { // Allocation failed
        free(new_node);
        return;
    }
    new_node->next = *head;
    *head = new_node;
}

static void free_seen_names(SeenName **head) {
    SeenName *current = *head;
    while (current != NULL) {
        SeenName *next = current->next;
        free(current->name);
        free(current);
        current = next;
    }
    *head = NULL;
}

static int split_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    (void) offset; // Unused parameter
    (void) fi;     // Unused parameter
    (void) flags;  // Unused parameter

    char fpath[PATH_MAX];
    fullpath(fpath, path);
    // fprintf(stderr, "DEBUG: split_readdir: path='%s', fpath='%s'\n", path, fpath);

    DIR *dp = opendir(fpath);
    if (!dp) {
        fprintf(stderr, "ERROR: split_readdir: opendir failed for %s: %s\n", fpath, strerror(errno));
        return -errno;
    }

    struct dirent *de;
    SeenName *seen_logical_files = NULL; // Track logical filenames we've added

    // Add . and .. first
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);


    while ((de = readdir(dp)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }

        char entry_fpath[PATH_MAX];
        snprintf(entry_fpath, PATH_MAX, "%s/%s", fpath, de->d_name);
        struct stat st;
        // We need lstat to determine file type correctly without following links
        // and to get basic info for filler.
        if (lstat(entry_fpath, &st) == -1) {
             fprintf(stderr, "WARNING: split_readdir: lstat failed for %s: %s. Skipping entry.\n", entry_fpath, strerror(errno));
             continue;
        }

        char base_name[PATH_MAX];
        int part_idx;
        if (parse_part_filename(de->d_name, base_name, sizeof(base_name), &part_idx)) {
            // It's a part file. Add the logical name if not already seen.
            if (!has_been_seen(&seen_logical_files, base_name)) {
                // We need *some* stats for the logical file. Get stats from part 0.
                struct stat logical_st;
                char logical_fpath[PATH_MAX];
                // Construct full path for the logical file name within the backing dir
                snprintf(logical_fpath, PATH_MAX, "%s/%s", fpath, base_name);
                int stat_res = get_split_file_stats(logical_fpath, &logical_st);

                if (stat_res == 0) {
                     // Use S_IFREG and mode from part0's stats
                     st.st_mode = logical_st.st_mode; // Should already be S_IFREG
                } else {
                    // Couldn't get stats for part 0? Problematic. Skip?
                    fprintf(stderr, "WARNING: split_readdir: Couldn't get stats for logical file %s based on part %s. Skipping.\n", base_name, de->d_name);
                    continue; // Skip this logical file
                }


                // fprintf(stderr, "DEBUG: split_readdir: Adding logical file %s\n", base_name);
                if (filler(buf, base_name, &st, 0, 0)) {
                    // fprintf(stderr, "DEBUG: split_readdir: Buffer full\n");
                    break; // Buffer full
                }
                add_seen_name(&seen_logical_files, base_name);
            }
            // Otherwise, we've already added the logical name, so skip this part file.
        } else {
            // It's a regular file or directory, not a part file. Add it directly.
            // fprintf(stderr, "DEBUG: split_readdir: Adding non-part entry %s\n", de->d_name);
             // Use the stats obtained from lstat earlier
            if (filler(buf, de->d_name, &st, 0, 0)) {
                // fprintf(stderr, "DEBUG: split_readdir: Buffer full\n");
                break; // Buffer full
            }
        }
    }

    closedir(dp);
    free_seen_names(&seen_logical_files);
    // fprintf(stderr, "DEBUG: split_readdir: Done for %s\n", path);
    return 0;
}

// Open a file - For split files, we just verify existence. For others, pass through.
static int split_open(const char *path, struct fuse_file_info *fi) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    // fprintf(stderr, "DEBUG: split_open: path='%s', flags=0x%x\n", path, fi->flags);

    struct stat stbuf;
    // Check if it's potentially a split file (does part 0 exist?)
    char part0_path[PATH_MAX];
    get_part_path(part0_path, fpath, 0);

    int res = lstat(part0_path, &stbuf);
    if (res == 0 && S_ISREG(stbuf.st_mode)) {
        // Part 0 exists and is a regular file. Assume it's a split file.
        // We don't need to keep an fd open for the logical file.
        // Check if write access is requested - is the directory writable?
        // The underlying pwrite calls will handle file permissions.
        // If O_TRUNC is specified, we should handle it here? No, truncate op handles it.
        // fprintf(stderr, "DEBUG: split_open: Opening logical split file %s\n", path);
        fi->fh = 0; // Indicate success, but no specific host fd for the logical file. Or use a magic number?
        return 0; // Success
    } else if (res == -1 && errno == ENOENT) {
        // Part 0 doesn't exist.
        // Could be a directory, a non-split file, or a non-existent file.
        // Let lstat figure it out.
        res = lstat(fpath, &stbuf);
        if (res == -1) {
             // File/Dir doesn't exist at all.
             if ((fi->flags & O_CREAT)) {
                 // Create will handle it, let open succeed conceptually for now?
                 // No, create op should be called. Open should fail if no O_CREAT.
                 // fprintf(stderr, "DEBUG: split_open: File %s doesn't exist (no O_CREAT)\n", path);
                 return -errno; // Return ENOENT
             } else {
                // File doesn't exist and no O_CREAT flag.
                // fprintf(stderr, "DEBUG: split_open: File %s doesn't exist\n", path);
                return -ENOENT;
             }
        }

        // It exists as something else (non-split file or directory).
        if (S_ISDIR(stbuf.st_mode)) {
             // Opening a directory. FUSE handles this internally? Or just succeed?
             // Let's treat it like a normal file open regarding fd.
             // Maybe need O_PATH? For now, mimic pass-through.
             // The 'persistent_open' example opened the dir, maybe we should too?
             // Let's skip opening dirs explicitly for simplicity now.
             // fprintf(stderr, "DEBUG: split_open: Trying to open directory %s\n", path);
              // Opening directories is often handled differently (e.g., by readdir).
              // Allow it but don't assign fd? Or return EISDIR? Let's return EISDIR.
             return -EISDIR;
        } else if (S_ISREG(stbuf.st_mode)) {
             // It's a regular, non-split file. Open it directly.
             // fprintf(stderr, "DEBUG: split_open: Opening regular non-split file %s\n", path);
             int fd = open(fpath, fi->flags);
             if (fd == -1) {
                 fprintf(stderr, "ERROR: split_open: open failed for non-split file %s: %s\n", fpath, strerror(errno));
                 return -errno;
             }
             fi->fh = fd; // Store the real fd
             return 0;
        } else {
            // It's something else (symlink, socket, etc.) - pass-through open?
            fprintf(stderr, "DEBUG: split_open: Opening non-regular, non-split file %s\n", path);
            int fd = open(fpath, fi->flags);
             if (fd == -1) {
                 fprintf(stderr, "ERROR: split_open: open failed for special file %s: %s\n", fpath, strerror(errno));
                 return -errno;
             }
             fi->fh = fd; // Store the real fd
            return 0;
        }
    } else {
        // Error other than ENOENT checking part0, or part0 is not a regular file.
        fprintf(stderr, "ERROR: split_open: Error checking part0 %s or it's not a regular file: %s\n", part0_path, strerror(errno));
        if (res == 0) return -EIO; // part0 exists but isn't a file
        return -errno; // Return the error from lstat
    }
}


// Create and open a file
static int split_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    // fprintf(stderr, "DEBUG: split_create: path='%s', mode=%o, flags=0x%x\n", path, mode, fi->flags);

    // We always create split files for this example.
    // Create the first part file (part 0) to mark the logical file's existence.
    char part0_path[PATH_MAX];
    get_part_path(part0_path, fpath, 0);

    // Ensure path doesn't contain the part suffix itself
    if (strstr(path, PART_SUFFIX)) {
        return -EINVAL; // Don't allow creating files ending in ".partN"
    }

    // Delete existing parts if O_TRUNC is implied by create (it usually is)
    // Check flags explicitly? open(2) with O_CREAT | O_EXCL fails if exists.
    // If O_CREAT without O_EXCL, it opens or truncates.
    // FUSE VFS layer usually sends O_CREAT | O_EXCL or O_CREAT | O_TRUNC.
    // Let's assume if create is called, we want a fresh file. Delete any old parts.
    // This might not be 100% correct POSIX if O_EXCL wasn't used and file existed.
    // A safer way might be to check existence first if O_EXCL is not in fi->flags.
    // For simplicity: always delete old parts on create.
    delete_all_parts(fpath); // Ignore error if it didn't exist

    // Create part 0
    // We need write access to create, and truncate it if it somehow exists.
    int fd = open(part0_path, O_CREAT | O_RDWR | O_TRUNC, mode);
    if (fd == -1) {
        fprintf(stderr, "ERROR: split_create: Failed to create part 0 %s: %s\n", part0_path, strerror(errno));
        // Attempt to clean up if directory creation failed midway? Complex.
        return -errno;
    }
    close(fd); // We don't keep it open

    // fprintf(stderr, "DEBUG: split_create: Created part 0 %s\n", part0_path);

    // As in open, no specific host fd for the logical file in split mode.
    fi->fh = 0; // Indicate success, no specific host fd
    return 0;
}


// Read data from an open file
static int split_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    // fprintf(stderr, "DEBUG: split_read: path='%s', size=%zu, offset=%ld\n", path, size, (long)offset);

    // Check if this corresponds to a non-split file opened earlier
    if (fi->fh != 0) { // Assuming 0 is our magic number for split files
        // fprintf(stderr, "DEBUG: split_read: Reading from non-split file (fh=%llu)\n", fi->fh);
        int res = pread(fi->fh, buf, size, offset);
        if (res == -1) {
            fprintf(stderr, "ERROR: split_read: pread failed for non-split file (fh=%llu): %s\n", fi->fh, strerror(errno));
            return -errno;
        }
        return res;
    }

    // Handle split file reading
    size_t total_bytes_read = 0;
    off_t current_offset = offset;

    while (total_bytes_read < size) {
        int part_idx = current_offset / CHUNK_SIZE;
        off_t offset_in_part = current_offset % CHUNK_SIZE;
        size_t bytes_to_read_from_this_part = CHUNK_SIZE - offset_in_part;
        size_t remaining_needed = size - total_bytes_read;

        if (bytes_to_read_from_this_part > remaining_needed) {
            bytes_to_read_from_this_part = remaining_needed;
        }

        char part_path[PATH_MAX];
        get_part_path(part_path, fpath, part_idx);

        int fd = open(part_path, O_RDONLY);
        if (fd == -1) {
            if (errno == ENOENT) {
                // We tried to read past the end of the file (no more parts)
                // fprintf(stderr, "DEBUG: split_read: Reached end of file (part %d not found)\n", part_idx);
                break;
            } else {
                // Error opening a part file
                fprintf(stderr, "ERROR: split_read: Failed to open part %s: %s\n", part_path, strerror(errno));
                return -errno; // Return the error
            }
        }

        // fprintf(stderr, "DEBUG: split_read: Reading %zu bytes from part %d (%s) at offset %ld\n",
        //         bytes_to_read_from_this_part, part_idx, part_path, (long)offset_in_part);

        ssize_t res = pread(fd, buf + total_bytes_read, bytes_to_read_from_this_part, offset_in_part);
        close(fd); // Close immediately after read

        if (res == -1) {
            fprintf(stderr, "ERROR: split_read: pread failed for part %s: %s\n", part_path, strerror(errno));
            // If an error occurs after some bytes read, POSIX allows returning either error or bytes read.
            // Returning the error is often safer. If total_bytes_read > 0, maybe return that? Let's return error.
            return -errno;
        }

        if (res == 0) {
            // Reached EOF for this part unexpectedly (maybe shorter than CHUNK_SIZE)
            // This means we've reached the end of the logical file.
            // fprintf(stderr, "DEBUG: split_read: Reached EOF in part %d (%s)\n", part_idx, part_path);
            break;
        }

        total_bytes_read += res;
        current_offset += res;

        // If pread returned fewer bytes than requested, it means we hit EOF for this part.
        if (res < bytes_to_read_from_this_part) {
            // fprintf(stderr, "DEBUG: split_read: Short read from part %d (%s), likely EOF\n", part_idx, part_path);
            break; // Stop reading
        }
    }

    // fprintf(stderr, "DEBUG: split_read: Finished read for %s, returning %zu bytes\n", path, total_bytes_read);
    return total_bytes_read;
}


// Write data to an open file
static int split_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
     char fpath[PATH_MAX];
    fullpath(fpath, path);
    // fprintf(stderr, "DEBUG: split_write: path='%s', size=%zu, offset=%ld\n", path, size, (long)offset);

    // Check if this corresponds to a non-split file opened earlier
    if (fi->fh != 0) { // Assuming 0 is our magic number for split files
        // fprintf(stderr, "DEBUG: split_write: Writing to non-split file (fh=%llu)\n", fi->fh);
        int res = pwrite(fi->fh, buf, size, offset);
        if (res == -1) {
             fprintf(stderr, "ERROR: split_write: pwrite failed for non-split file (fh=%llu): %s\n", fi->fh, strerror(errno));
            return -errno;
        }
        // Should update underlying file timestamps? FUSE might handle this.
        return res;
    }

     // Handle split file writing
    size_t total_bytes_written = 0;
    off_t current_offset = offset;

    // Get mode from part 0 for creating subsequent parts
    mode_t mode = 0644; // Default if getattr fails
    struct stat st0;
    char part0_path[PATH_MAX];
    get_part_path(part0_path, fpath, 0);
    if (lstat(part0_path, &st0) == 0) {
        mode = st0.st_mode & 0777; // Get permission bits
    } else {
        fprintf(stderr, "WARNING: split_write: Could not stat part 0 %s to get mode. Using default.\n", part0_path);
    }


    while (total_bytes_written < size) {
        int part_idx = current_offset / CHUNK_SIZE;
        off_t offset_in_part = current_offset % CHUNK_SIZE;
        size_t bytes_to_write_to_this_part = CHUNK_SIZE - offset_in_part;
        size_t remaining_to_write = size - total_bytes_written;

        if (bytes_to_write_to_this_part > remaining_to_write) {
            bytes_to_write_to_this_part = remaining_to_write;
        }

        char part_path[PATH_MAX];
        get_part_path(part_path, fpath, part_idx);

        // Open with O_CREAT as writing might extend the file into new parts
        int fd = open(part_path, O_WRONLY | O_CREAT, mode);
        if (fd == -1) {
             fprintf(stderr, "ERROR: split_write: Failed to open/create part %s: %s\n", part_path, strerror(errno));
            // If we already wrote some bytes, should we return them or error?
            // Let's return error, the write operation failed.
            return -errno;
        }

        // fprintf(stderr, "DEBUG: split_write: Writing %zu bytes to part %d (%s) at offset %ld\n",
        //         bytes_to_write_to_this_part, part_idx, part_path, (long)offset_in_part);

        ssize_t res = pwrite(fd, buf + total_bytes_written, bytes_to_write_to_this_part, offset_in_part);
        close(fd); // Close immediately after write

        if (res == -1) {
            fprintf(stderr, "ERROR: split_write: pwrite failed for part %s: %s\n", part_path, strerror(errno));
             // Return error, potentially partial write happened but failed to complete.
            return -errno;
        }

        if (res < bytes_to_write_to_this_part) {
             // pwrite returning less than requested usually indicates an error (e.g., disk full)
             fprintf(stderr, "ERROR: split_write: pwrite wrote fewer bytes (%zd) than requested (%zu) for part %s. Disk full?\n",
                     res, bytes_to_write_to_this_part, part_path);
             // Return the number of bytes actually written before the error occurred?
             // Or return an error? Let's return the bytes written for now.
             total_bytes_written += res;
             break; // Stop writing
        }

        total_bytes_written += res;
        current_offset += res;
    }

    // Update timestamps on part 0? Or let OS handle timestamps on modified parts?
    // Maybe touch part 0 to update mtime?
    // utimes(part0_path, NULL); // Update access and mod times to now

    // fprintf(stderr, "DEBUG: split_write: Finished write for %s, returning %zu bytes\n", path, total_bytes_written);
    return total_bytes_written;
}


// Release (close) an open file
static int split_release(const char *path, struct fuse_file_info *fi) {
    // fprintf(stderr, "DEBUG: split_release: path='%s', fh=%llu\n", path, fi->fh);
    // If fh corresponds to a real fd for a non-split file, close it.
    if (fi->fh != 0) { // Assuming 0 is our magic number for split files
        // fprintf(stderr, "DEBUG: split_release: Closing non-split file (fh=%llu)\n", fi->fh);
        close(fi->fh);
    } else {
        // No action needed for split files, as parts are opened/closed per read/write
        // fprintf(stderr, "DEBUG: split_release: No action for split file %s\n", path);
    }
    return 0;
}

// Truncate a file
static int split_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    fprintf(stderr, "DEBUG: split_truncate: path='%s', size=%ld\n", path, (long)size);

    // If fi is available and fh is non-zero, it's a non-split file.
    // However, truncate can be called without an open file context (fi == NULL).
    // So, we must check if the file is split regardless of fi.

    struct stat stbuf;
    bool is_split = (get_split_file_stats(fpath, &stbuf) == 0);

    if (!is_split) {
        // If get_split_file_stats failed (e.g. ENOENT), check if it's a normal file/dir
         if (lstat(fpath, &stbuf) == -1) {
             fprintf(stderr,"ERROR: split_truncate: Target %s does not exist\n", fpath);
             return -errno; // File not found
         }
         if (S_ISDIR(stbuf.st_mode)) {
              fprintf(stderr,"ERROR: split_truncate: Cannot truncate directory %s\n", fpath);
              return -EISDIR;
         }
         // Assume it's a regular non-split file
         fprintf(stderr, "DEBUG: split_truncate: Truncating non-split file %s\n", fpath);
         int res = truncate(fpath, size);
         if (res == -1) {
              fprintf(stderr,"ERROR: split_truncate: truncate failed for non-split file %s: %s\n", fpath, strerror(errno));
             return -errno;
         }
         return 0;
    }

    // Handle truncation of a split file
    fprintf(stderr, "DEBUG: split_truncate: Truncating split file %s to %ld\n", fpath, (long)size);

    if (size < 0) return -EINVAL; // Invalid size

    if (size == 0) {
        // Truncate to zero: delete all parts.
        fprintf(stderr, "DEBUG: split_truncate: Deleting all parts for size 0\n");
        return delete_all_parts(fpath);
    }

    // Calculate target state
    int target_max_part_idx = (size - 1) / CHUNK_SIZE;
    off_t target_last_part_size = (size - 1) % CHUNK_SIZE + 1;

    fprintf(stderr, "DEBUG: split_truncate: Target max part=%d, last part size=%ld\n", target_max_part_idx, (long)target_last_part_size);

    // Find current highest part index
    int current_max_part_idx = -1;
    int part_idx = 0;
    char part_path[PATH_MAX];
    while (true) {
        get_part_path(part_path, fpath, part_idx);
        if (lstat(part_path, &stbuf) == -1) {
            if (errno == ENOENT) {
                current_max_part_idx = part_idx - 1;
                break;
            } else {
                 fprintf(stderr,"ERROR: split_truncate: Error finding current parts for %s: %s\n", part_path, strerror(errno));
                 return -errno; // Error accessing parts
            }
        }
        part_idx++;
    }
    fprintf(stderr, "DEBUG: split_truncate: Current max part=%d\n", current_max_part_idx);

    // Delete extra parts if shrinking
    if (target_max_part_idx < current_max_part_idx) {
        fprintf(stderr, "DEBUG: split_truncate: Deleting parts from %d to %d\n", target_max_part_idx + 1, current_max_part_idx);
        for (part_idx = target_max_part_idx + 1; part_idx <= current_max_part_idx; ++part_idx) {
            get_part_path(part_path, fpath, part_idx);
            if (unlink(part_path) == -1 && errno != ENOENT) {
                 fprintf(stderr,"ERROR: split_truncate: Failed to delete part %s: %s\n", part_path, strerror(errno));
                 return -errno; // Fail early on error
            }
        }
    }

    // Truncate the target last part
    if (target_max_part_idx >= 0) {
        get_part_path(part_path, fpath, target_max_part_idx);
        fprintf(stderr, "DEBUG: split_truncate: Truncating part %d (%s) to %ld\n", target_max_part_idx, part_path, (long)target_last_part_size);
        // Need to handle case where this part doesn't exist yet (extending)
        int fd = open(part_path, O_WRONLY | O_CREAT, stbuf.st_mode & 0777); // Use mode from existing parts or default? Get mode from part0.
        if (fd == -1) {
             fprintf(stderr,"ERROR: split_truncate: Failed to open/create target part %s: %s\n", part_path, strerror(errno));
             return -errno;
        }
        int res = ftruncate(fd, target_last_part_size);
         close(fd);
        if (res == -1) {
             fprintf(stderr,"ERROR: split_truncate: ftruncate failed for part %s: %s\n", part_path, strerror(errno));
            return -errno;
        }
    }

    // Zero-fill intermediate parts if extending?
    // ftruncate on some systems might create sparse files. If not, we might need
    // to explicitly write zeros, which is much more complex. Let's assume
    // ftruncate handles extending correctly (either sparse or zero-fills).

    return 0;
}

// Create a directory (pass-through)
static int split_mkdir(const char *path, mode_t mode) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    // fprintf(stderr, "DEBUG: split_mkdir: path='%s', mode=%o\n", path, mode);
    // Prevent creating directories with part suffix? Maybe not necessary.
    int res = mkdir(fpath, mode);
    if (res == -1) {
         fprintf(stderr,"ERROR: split_mkdir: mkdir failed for %s: %s\n", fpath, strerror(errno));
        return -errno;
    }
    return 0;
}

// Remove a file
static int split_unlink(const char *path) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    fprintf(stderr, "DEBUG: split_unlink: path='%s'\n", path);

     // Is the requested path itself a part file? Should not happen if getattr hides them.
    if (strstr(path, PART_SUFFIX)) return -ENOENT;


    // Try deleting as a split file first.
    int res = delete_all_parts(fpath);

    if (res == 0) {
        fprintf(stderr, "DEBUG: split_unlink: Deleted split file %s\n", path);
        return 0; // Success
    } else if (res == -ENOENT) {
        // No parts found. Maybe it's a non-split file or doesn't exist.
        fprintf(stderr, "DEBUG: split_unlink: No parts found for %s, trying regular unlink\n", path);
        res = unlink(fpath); // Try unlinking the base path directly
        if (res == -1) {
             fprintf(stderr, "ERROR: split_unlink: unlink failed for non-split file %s: %s\n", fpath, strerror(errno));
            // If unlink fails with ENOENT, then the file really didn't exist.
            // If it fails with EPERM etc., return that error.
             return -errno;
        }
        fprintf(stderr, "DEBUG: split_unlink: Unlinked non-split file %s\n", path);
        return 0; // Success deleting non-split file
    } else {
        // Error occurred during delete_all_parts
        fprintf(stderr, "ERROR: split_unlink: Error %d deleting parts for %s\n", res, path);
        return res;
    }
}

// Remove a directory (pass-through)
static int split_rmdir(const char *path) {
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    // fprintf(stderr, "DEBUG: split_rmdir: path='%s'\n", path);
    int res = rmdir(fpath);
    if (res == -1) {
        fprintf(stderr, "ERROR: split_rmdir: rmdir failed for %s: %s\n", fpath, strerror(errno));
        return -errno;
    }
    return 0;
}

// Rename a file or directory
static int split_rename(const char *from, const char *to, unsigned int flags) {
	char fpath_from[PATH_MAX];
    char fpath_to[PATH_MAX];
    fullpath(fpath_from, from);
    fullpath(fpath_to, to);

    fprintf(stderr, "DEBUG: split_rename: from='%s', to='%s', flags=%u\n", from, to, flags);

    // Check for RENAME_NOREPLACE etc. (optional for this example)
	#ifdef RENAME_NOREPLACE
	if (flags & RENAME_NOREPLACE) {
		// Check if target exists, fail if it does
        struct stat st_to;
        if (lstat(fpath_to, &st_to) == 0 || get_split_file_stats(fpath_to, &st_to) == 0) {
             fprintf(stderr,"ERROR: split_rename: Target %s exists (RENAME_NOREPLACE)\n", to);
             return -EEXIST;
        }
	}
    // RENAME_EXCHANGE is more complex, not handled here
    if (flags & RENAME_EXCHANGE) return -EINVAL;
	#else
    if (flags != 0) return -EINVAL; // Reject flags if not supported
    #endif

    // Check if 'from' is a split file
    struct stat st_from;
    bool from_is_split = (get_split_file_stats(fpath_from, &st_from) == 0);

    // Check if 'to' might conflict (is it a part file name?)
     if (strstr(to, PART_SUFFIX)) {
         fprintf(stderr,"ERROR: split_rename: Cannot rename *to* a part file name: %s\n", to);
         return -EINVAL;
     }


    if (!from_is_split) {
        // 'from' is not a split file (or doesn't exist). Assume it's a regular file/dir.
        // Let the standard rename handle it.
        fprintf(stderr, "DEBUG: split_rename: Renaming non-split %s -> %s\n", from, to);
        int res = rename(fpath_from, fpath_to);
        if (res == -1) {
            fprintf(stderr, "ERROR: split_rename: rename failed for non-split: %s\n", strerror(errno));
            return -errno;
        }
        return 0;
    }

    // 'from' IS a split file. We need to rename all its parts.
    // WARNING: This is NOT ATOMIC.
    fprintf(stderr, "DEBUG: split_rename: Renaming split file %s -> %s (part by part)\n", from, to);

    // If 'to' exists (as split or non-split), delete it first (unless RENAME_NOREPLACE checked above)
    if (!(flags & RENAME_NOREPLACE)) { // Check flag definedness again? No, handled above. Assume flags=0 if undefined.
        struct stat st_to;
        bool to_is_split = (get_split_file_stats(fpath_to, &st_to) == 0);
        int del_res = 0;
        if (to_is_split) {
            fprintf(stderr, "DEBUG: split_rename: Target %s is split file, deleting parts first\n", to);
            del_res = delete_all_parts(fpath_to);
        } else if (lstat(fpath_to, &st_to) == 0) {
             // Target exists as non-split file/dir
             fprintf(stderr, "DEBUG: split_rename: Target %s is non-split, unlinking/rmdiring first\n", to);
            if (S_ISDIR(st_to.st_mode)) del_res = rmdir(fpath_to);
            else del_res = unlink(fpath_to);
            if (del_res == -1) del_res = -errno; // Convert to negative errno
        }
        // If deletion failed (and it wasn't because it didn't exist), abort rename.
        if (del_res != 0 && del_res != -ENOENT) {
             fprintf(stderr,"ERROR: split_rename: Failed to remove existing target %s: %d\n", to, del_res);
             return del_res;
        }
    }


    int part_idx = 0;
    char part_from_path[PATH_MAX];
    char part_to_path[PATH_MAX];
    int last_errno = 0; // Record first error
    bool renamed_any = false;

    while(true) {
        get_part_path(part_from_path, fpath_from, part_idx);
        get_part_path(part_to_path, fpath_to, part_idx);

        int res = rename(part_from_path, part_to_path);
        if (res == -1) {
            if (errno == ENOENT) {
                 // No more parts exist for 'from'
                 if (part_idx == 0 && !renamed_any) {
                     // Part 0 didn't exist? Should have been caught by from_is_split check. Error.
                      fprintf(stderr,"ERROR: split_rename: Part 0 %s not found for supposedly split file\n", part_from_path);
                      // Might need cleanup of already renamed parts if any? Complex. Abort.
                      // This state indicates an inconsistency.
                      return -EIO;
                 }
                 // Normal termination: we have renamed all existing parts.
                 break;
            } else {
                // Real error renaming a part.
                fprintf(stderr,"ERROR: split_rename: Failed to rename part %s to %s: %s\n", part_from_path, part_to_path, strerror(errno));
                // ABORT. State is inconsistent. Return the error.
                // TODO: Ideally, try to rename back already moved parts. Very difficult.
                last_errno = errno;
                break; // Stop trying
            }
        } else {
            renamed_any = true; // Mark that we successfully renamed at least one part
        }
        part_idx++;
    }

    return -last_errno; // Return 0 if successful, -error otherwise
}

// Define the FUSE operations structure
static const struct fuse_operations split_oper = {
    .getattr    = split_getattr,
    .readdir    = split_readdir,
    .open       = split_open,
    .create     = split_create,
    .read       = split_read,
    .write      = split_write,
    .release    = split_release,
    .truncate   = split_truncate,
    .mkdir      = split_mkdir,   // Pass-through mkdir
    .unlink     = split_unlink,
    .rmdir      = split_rmdir,   // Pass-through rmdir
    .rename     = split_rename,
    // Add other operations like symlink, link, chmod, chown etc. as needed
    // These would likely need modification if they operate on split files.
};

int main(int argc, char *argv[]) {
    // --- Path Setup ---
    if (realpath(BACKING_DIR_REL, backing_dir_abs) == NULL) {
         if (mkdir(BACKING_DIR_REL, 0755) == 0) {
             fprintf(stderr, "Created backing directory: %s\n", BACKING_DIR_REL);
             if (realpath(BACKING_DIR_REL, backing_dir_abs) == NULL) {
                 perror("realpath after mkdir failed");
                 return 1;
             }
         } else if (errno == EEXIST) {
             if (realpath(BACKING_DIR_REL, backing_dir_abs) == NULL) {
                 perror("realpath on existing dir failed");
                 return 1;
             }
         } else {
             perror("mkdir failed");
             fprintf(stderr, "Error: Could not create or find backing directory: %s\n", BACKING_DIR_REL);
             return 1;
         }
    }
    fprintf(stderr, "Using backing directory: %s\n", backing_dir_abs);
    fprintf(stderr, "Using chunk size: %d bytes\n", CHUNK_SIZE);

    struct stat st;
    if (stat(backing_dir_abs, &st) == -1) {
        perror("stat on backing directory failed");
        return 1;
    }
    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr,"Error: Backing path '%s' is not a directory.\n", backing_dir_abs);
        return 1;
    }
    // --- End Path Setup ---

    fprintf(stderr, "Starting FUSE filesystem...\n");
    // Pass arguments directly to fuse_main
    int ret = fuse_main(argc, argv, &split_oper, NULL);
    fprintf(stderr, "FUSE filesystem exiting (return code %d).\n", ret);
    return ret;
}