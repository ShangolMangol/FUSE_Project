# FUSE Project

## Compilation
To compile the project:
```bash
make
```

This will build:
- CriticalFUSE (the main FUSE filesystem)
- HandlerTest (for testing file handlers)
- BitFlipper (for bit manipulation)

### Makefile Targets
The Makefile provides several targets:

- `make` or `make all`: Builds all executables
- `make clean`: Removes all compiled objects and executables
- `make run`: Runs the HandlerTest executable
- `make run_fuse`: Runs the CriticalFUSE filesystem in foreground mode

To build specific components:
```bash
make CriticalFUSE    # Build only the FUSE filesystem
make HandlerTest     # Build only the handler test
make BitFlipper      # Build only the bit flipper tool
```

## Running the FUSE Filesystem
```bash
./CriticalFUSE -f mnt
```
- `-f`: Run in foreground
- `-d`: Enable debug output

To unmount:
```bash
fusermount3 -u ./mnt
```

## BitFlipper Tool
The BitFlipper tool allows you to flip bits in files, either completely or randomly. This is useful for testing file corruption scenarios and error resilience.

### Usage

1. Normal Mode (flip all bits in a range):
```bash
./BitFlipper <file> <start_offset> <end_offset>
```
Example:
```bash
./BitFlipper test.txt 0 1000  # Flips all bits in bytes 0-1000
```

2. Random Mode (flip random bits in entire file):
```bash
./BitFlipper -r <percentage> <file>
```
Example:
```bash
./BitFlipper -r 25 test.txt  # Randomly flips 25% of all bits in the file
```

### Features
- Normal mode: Flips all bits in a specified range
- Random mode: Randomly flips a specified percentage of bits throughout the entire file
- Efficient processing using buffered I/O
- Proper error handling and validation
- Progress reporting

### Notes
- The random mode uses a uniform distribution to ensure even spread of bit flips
- File modifications are done in-place
- Always make a backup of important files before using the BitFlipper

## Project Structure
The project is organized as follows:
- `FUSE/`: Contains the main FUSE filesystem implementation
- `FileHandlers/`: Contains handlers for different file types
- `Utilities/`: Contains utility classes and functions
- `Makefile`: Build configuration for the project

