#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

#define BUFFER_SIZE (1024 * 1024)  // 1MB buffer

void print_usage(const char *program_name) {
    fprintf(stderr, "Usage: %s <file> <start_offset> <end_offset>\n", program_name);
    fprintf(stderr, "All bytes in [start_offset, end_offset] inclusive will be bitwise inverted.\n");
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        print_usage(argv[0]);
        return 1;
    }

    const char *filename = argv[1];
    long start_offset = strtol(argv[2], NULL, 10);
    long end_offset = strtol(argv[3], NULL, 10);

    // Validate inputs
    if (start_offset < 0 || end_offset < 0) {
        fprintf(stderr, "Error: Offsets must be non-negative\n");
        return 1;
    }

    if (start_offset > end_offset) {
        fprintf(stderr, "Error: start_offset must be <= end_offset\n");
        return 1;
    }

    // Get file size
    struct stat st;
    if (stat(filename, &st) != 0) {
        fprintf(stderr, "Error: Cannot stat file '%s': %s\n", filename, strerror(errno));
        return 1;
    }

    if (end_offset >= st.st_size) {
        fprintf(stderr, "Error: end_offset (%ld) is beyond end of file (file size: %ld bytes)\n",
                end_offset, (long)st.st_size);
        return 1;
    }

    // Open file for reading and writing
    FILE *file = fopen(filename, "r+b");
    if (!file) {
        fprintf(stderr, "Error: Cannot open file '%s': %s\n", filename, strerror(errno));
        return 1;
    }

    // Calculate total bytes to process
    size_t total_bytes = end_offset - start_offset + 1;
    size_t bytes_processed = 0;
    unsigned char *buffer = malloc(BUFFER_SIZE);
    
    if (!buffer) {
        fprintf(stderr, "Error: Failed to allocate memory\n");
        fclose(file);
        return 1;
    }

    // Process file in chunks
    while (bytes_processed < total_bytes) {
        // Calculate chunk size
        size_t chunk_size = BUFFER_SIZE;
        if (chunk_size > (total_bytes - bytes_processed)) {
            chunk_size = total_bytes - bytes_processed;
        }

        // Seek to correct position
        if (fseek(file, start_offset + bytes_processed, SEEK_SET) != 0) {
            fprintf(stderr, "Error: Failed to seek in file: %s\n", strerror(errno));
            free(buffer);
            fclose(file);
            return 1;
        }

        // Read chunk
        size_t bytes_read = fread(buffer, 1, chunk_size, file);
        if (bytes_read != chunk_size) {
            fprintf(stderr, "Error: Failed to read from file: %s\n", strerror(errno));
            free(buffer);
            fclose(file);
            return 1;
        }

        // Flip bits in buffer
        for (size_t i = 0; i < bytes_read; i++) {
            buffer[i] = ~buffer[i];  // Bitwise NOT operation
        }

        // Seek back to write position
        if (fseek(file, start_offset + bytes_processed, SEEK_SET) != 0) {
            fprintf(stderr, "Error: Failed to seek in file: %s\n", strerror(errno));
            free(buffer);
            fclose(file);
            return 1;
        }

        // Write flipped data back
        size_t bytes_written = fwrite(buffer, 1, bytes_read, file);
        if (bytes_written != bytes_read) {
            fprintf(stderr, "Error: Failed to write to file: %s\n", strerror(errno));
            free(buffer);
            fclose(file);
            return 1;
        }

        bytes_processed += bytes_read;
    }

    // Cleanup
    free(buffer);
    fclose(file);

    printf("Successfully flipped all bits in bytes from offset %ld to %ld in %s\n",
           start_offset, end_offset, filename);
    return 0;
} 