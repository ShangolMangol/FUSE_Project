# FUSE Project

## Table of Contents
- [Supported File Types](#supported-file-types)
- [Compilation](#compilation)
- [Running the FUSE Filesystem](#running-the-fuse-filesystem)
- [Adding a New File Handler](#adding-a-new-file-handler)
- [BitFlipper Tool](#bitflipper-tool)
- [Project Structure](#project-structure)

## Supported File Types

CriticalFuse currently supports the following file types:

### ✅ **Fully Supported**
- **Text files** (`.txt`) - Complete support with critical/non-critical data separation
- **JPEG images** (`.jpg`, `.jpeg`) - Full support with GuetzliSplit integration
- **PNG images** (`.png`) - Supports partitioning but **cannot accept bit flips** due to CRC checking
- **BMP images** (`.bmp`) - Full support with critical/non-critical data separation

### ⚠️ **Experimental/Not Supported**
- **DNG files** (`.dng`) - Experimental implementation, not recommended for production use

### 📝 **Notes**
- **PNG files**: While PNG files support critical/non-critical data partitioning, they cannot tolerate bit flips in the non-critical sections due to PNG's built-in CRC (Cyclic Redundancy Check) validation. Any bit corruption will cause the PNG to be rejected as invalid.
- **DNG file handlers**: These exist in the codebase but are experimental and may not work correctly
- **Experimental handlers**: Should not be used in production environments
- **For reliable operation**: Stick to the fully supported file types listed above

## Compilation
To compile the project inside the folder `CriticalFuse`:
```bash
make
```

This will build:
- CriticalFUSE (the main FUSE filesystem)
- HandlerTest (for testing txt file split)
- BitFlipper (for bit manipulation over non-critical data)

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

To unmount (needed if run in the background):
```bash
fusermount3 -u ./mnt
```

## Adding a New File Handler

To add support for a new file type in the Critical Fuse system, follow these steps:

1. **Create a New Handler Class:**
   - In the `CriticalFuse/FileHandlers/` directory, create a new C++ class for your file type (e.g., `MyFileHandler.cpp` and `MyFileHandler.h`).
   - Inherit from the abstract base class `AbstractFileHandler`.

2. **Implement Required Methods:**
   - Implement the mapping function specific to your file type, as well as any other virtual methods from `AbstractFileHandler` for advanced usage.
   - Example:
     ```cpp
     // MyFileHandler.h
     #include "AbstractFile.h"
     class MyFileHandler : public AbstractFileHandler {
     public:
         MyFileHandler(const std::string& path);
         ResultCode createMapping(const char* buffer, size_t size) override;
         // Implement other required methods...
     };
     ```

3. **Register the New Handler:**
   - In the main FUSE (`CriticalFUSE.cpp`), locate the section where file handlers are selected based on file type (in the function `getFileHandler`).
   - Add an `if` or `switch` statement to instantiate your new handler for the appropriate file extension.
   - Example:
     ```cpp
     if (extension == ".mytype") {
         fileHandler = std::make_unique<MyFileHandler>(path);
     }
     ```

4. **Update the Build System:**
   - Add your new `.cpp` file to the `CriticalFuse/Makefile` so it is compiled and linked.

5. **Run The System With Your Handler:**
   - Build the project with `make` and test your handler using the FUSE filesystem.

For more details, refer to the existing handlers in `CriticalFuse/FileHandlers/` (e.g., `BmpFile.cpp`, `TextFile.cpp`).

**Note:** While `DngFile.cpp` exists in the codebase, this is an experimental implementation and is not supported for production use. It should not be used as a reference implementation for new file handlers.

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

### BitFlipper Notes
- The random mode uses a uniform distribution to ensure even spread of bit flips
- File modifications are done in-place
- Always make a backup of important files before using the BitFlipper

## Project Structure

This FUSE project contains multiple filesystem implementations and supporting tools organized as follows:

### Main Project Folders

#### `CriticalFuse/` - Main FUSE Implementation
The primary and most advanced FUSE filesystem implementation featuring:
- **`FUSE/`**: Contains the main FUSE filesystem implementation (`CriticalFUSE.cpp`)
- **`FileHandlers/`**: Contains specialized handlers for different file types:
  - `AbstractFile.cpp/h`: Base class for all file handlers
  - `BmpFile.cpp/h`: BMP image file handler
  - `DngFile.cpp/h`: DNG image file handler (⚠️ **NOT SUPPORTED** - experimental only)
  - `JpegFile.cpp/h`: JPEG image file handler
  - `PngFile.cpp/h`: PNG image file handler
  - `TextFile.cpp/h`: Text file handler
- **`GuetzliSplit/`**: JPEG compression and splitting library (Google's Guetzli implementation)
- **`Utilities/`**: Contains utility classes and functions (`Range.cpp/h`)
- **`Measurements/`**: Comprehensive analysis tools for performance and reliability testing:
  - `bit_flip_analysis.py`: Analyzes impact of bit corruption on non-critical files
  - `jpeg_performance_test.py`: Measures GuetzliSplit operation performance
  - `storage_overhead_analysis.py`: Analyzes storage efficiency and overhead
- **`tests/`**: Complete test suite for filesystem validation:
  - `test_basic_operations.sh`: Basic filesystem operations testing
  - `test_file_types.sh`: Different file type handling tests
  - `test_stress.sh`: Stress tests and edge cases
  - `run_all_tests.sh`: Master test runner
- **`TestImages/`**: Collection of test images for validation and performance testing
- **`BitFlipper.c`**: Tool for bit manipulation and corruption testing
- **`Makefile`**: Build configuration for the project

#### `BasicFilesystem/` - Simple FUSE Implementation
A basic FUSE filesystem implementation for learning and reference:
- `basicFuse.c`: Simple FUSE filesystem with basic file operations

#### `BasicSplitFilesystem/` - Split Storage FUSE
A FUSE implementation that demonstrates file splitting across two storage parts:
- `splitTwoPartsFuse.c`: FUSE filesystem that splits files into two parts for storage

#### `GuetzliSplit/` - JPEG Compression Library
Standalone Guetzli JPEG compression library:
- `guetzli-master/`: Complete Google Guetzli source code
- `guetzli-source-code.zip`: Compressed source archive
- `sample-jpeg-16x16.jpg`: Sample JPEG for testing

