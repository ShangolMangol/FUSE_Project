#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>

#define BUFFER_SIZE (1024 * 1024)  // 1MB buffer

void print_usage(const char *program_name) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  Normal mode: %s <file> <start_offset> <end_offset>\n", program_name);
    fprintf(stderr, "  Random mode: %s -r <percentage> <file>\n", program_name);
    fprintf(stderr, "  In normal mode, bytes in [start_offset, end_offset] will be bitwise inverted.\n");
    fprintf(stderr, "  In random mode, <percentage> of the entire file's bytes will be randomly flipped.\n");
}

// Function to flip bits in a buffer
void flip_bits_in_buffer(unsigned char *buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        buffer[i] = ~buffer[i];  // Bitwise NOT operation
    }
}

// Function to randomly flip bits in a buffer based on percentage
void random_flip_bits_in_buffer(unsigned char *buffer, size_t size, double percentage) {
    // Calculate how many bytes to flip
    size_t bytes_to_flip = (size_t)((size * percentage) / 100.0);
    
    // Create an array of indices
    size_t *indices = malloc(size * sizeof(size_t));
    if (!indices) {
        fprintf(stderr, "Error: Failed to allocate memory for indices\n");
        return;
    }
    
    // Initialize indices array
    for (size_t i = 0; i < size; i++) {
        indices[i] = i;
    }
    
    // Shuffle indices using Fisher-Yates algorithm
    for (size_t i = size - 1; i > 0; i--) {
        size_t j = rand() % (i + 1);
        // Swap indices[i] and indices[j]
        size_t temp = indices[i];
        indices[i] = indices[j];
        indices[j] = temp;
    }
    
    // Flip the first 'bytes_to_flip' bytes at the shuffled positions
    for (size_t i = 0; i < bytes_to_flip; i++) {
        buffer[indices[i]] = ~buffer[indices[i]];
    }
    
    free(indices);
}

int main(int argc, char *argv[]) {
    int random_mode = 0;
    int arg_offset = 1;
    double percentage = 0.0;

    // Check for random flag
    if (argc > 1 && (strcmp(argv[1], "-r") == 0 || strcmp(argv[1], "--random") == 0)) {
        random_mode = 1;
        arg_offset = 2;
        
        // Check if we have enough arguments for random mode
        if (argc != 4) {
            print_usage(argv[0]);
            return 1;
        }
        
        // Parse percentage
        percentage = strtod(argv[2], NULL);
        if (percentage < 0.0 || percentage > 100.0) {
            fprintf(stderr, "Error: Percentage must be between 0 and 100\n");
            return 1;
        }
        arg_offset = 3;
    } else if (argc != 4) {
        print_usage(argv[0]);
        return 1;
    }

    // Initialize random seed
    if (random_mode) {
        srand(time(NULL));
    }

    const char *filename = argv[arg_offset];
    long start_offset = 0;
    long end_offset = 0;

    // Get file size
    struct stat st;
    if (stat(filename, &st) != 0) {
        fprintf(stderr, "Error: Cannot stat file '%s': %s\n", filename, strerror(errno));
        return 1;
    }

    if (random_mode) {
        // In random mode, use the entire file
        end_offset = st.st_size - 1;
    } else {
        // In normal mode, use provided offsets
        start_offset = strtol(argv[arg_offset + 1], NULL, 10);
        end_offset = strtol(argv[arg_offset + 2], NULL, 10);

        // Validate offsets
        if (start_offset < 0 || end_offset < 0) {
            fprintf(stderr, "Error: Offsets must be non-negative\n");
            return 1;
        }

        if (start_offset > end_offset) {
            fprintf(stderr, "Error: start_offset must be <= end_offset\n");
            return 1;
        }

        if (end_offset >= st.st_size) {
            fprintf(stderr, "Error: end_offset (%ld) is beyond end of file (file size: %ld bytes)\n",
                    end_offset, (long)st.st_size);
            return 1;
        }
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

        // Flip bits in buffer (either all or randomly)
        if (random_mode) {
            random_flip_bits_in_buffer(buffer, bytes_read, percentage);
        } else {
            flip_bits_in_buffer(buffer, bytes_read);
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

    if (random_mode) {
        printf("Successfully randomly flipped %.1f%% of bits in the entire file %s\n",
               percentage, filename);
    } else {
        printf("Successfully flipped all bits in bytes from offset %ld to %ld in %s\n",
               start_offset, end_offset, filename);
    }
    return 0;
} 