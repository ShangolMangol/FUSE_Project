# CriticalFuse - Advanced FUSE Filesystem Implementation

CriticalFuse is an advanced FUSE (Filesystem in Userspace) implementation that provides intelligent file handling with critical/non-critical data separation. This filesystem is designed to handle various file types with specialized processing and error resilience capabilities.

## Table of Contents
- [Overview](#overview)
- [Directory Structure](#directory-structure)
- [Quick Start](#quick-start)
- [Supported File Types](#supported-file-types)
- [Build System](#build-system)
- [Testing](#testing)
- [Performance Analysis](#performance-analysis)
- [Development](#development)

## Overview

CriticalFuse implements a sophisticated filesystem that:
- Separates files into critical and non-critical data sections
- Provides specialized handlers for different file types
- Supports bit flip tolerance for non-critical data
- Integrates with Google's Guetzli JPEG compression library
- Includes comprehensive testing and analysis tools

## Directory Structure

### Core Components

#### `FUSE/`
Contains the main FUSE filesystem implementation.
- **`CriticalFUSE.cpp`**: The primary FUSE filesystem implementation that handles all filesystem operations, file type detection, and routing to appropriate handlers.

#### `FileHandlers/`
Specialized handlers for different file types that implement critical/non-critical data separation.
- **`AbstractFile.cpp/h`**: Base class defining the interface for all file handlers
- **`TextFile.cpp/h`**: Handler for text files (`.txt`) - fully supported
- **`JpegFile.cpp/h`**: Handler for JPEG images (`.jpg`, `.jpeg`) - fully supported with GuetzliSplit integration
- **`PngFile.cpp/h`**: Handler for PNG images (`.png`) - supports partitioning but cannot accept bit flips due to CRC checking
- **`BmpFile.cpp/h`**: Handler for BMP images (`.bmp`) - fully supported
- **`DngFile.cpp/h`**: Handler for DNG images (`.dng`) - ⚠️ **EXPERIMENTAL ONLY, NOT SUPPORTED**
- **`RawFile.cpp/h`**: Generic raw file handler - ⚠️ **EXPERIMENTAL ONLY, NOT SUPPORTED**

#### `GuetzliSplit/`
Google's Guetzli JPEG compression library implementation.
- Contains the complete Guetzli source code for JPEG compression and optimization
- Used by the JPEG file handler for advanced compression and splitting operations
- Includes DCT (Discrete Cosine Transform), quantization, and entropy encoding modules

#### `Utilities/`
Utility classes and helper functions.
- **`Range.cpp/h`**: Utility class for handling data ranges and offsets in file operations

### Testing and Analysis

#### `tests/`
Comprehensive test suite for filesystem validation.
- **`run_all_tests.sh`**: Master test runner that executes all test suites
- **`test_basic_operations.sh`**: Tests basic filesystem operations (create, read, write, delete)
- **`test_file_types.sh`**: Tests different file type handling capabilities
- **`test_stress.sh`**: Stress tests and edge cases (large files, concurrent operations)
- **`test_assets/`**: Test files used by the test suite
- **`README.md`**: Detailed testing documentation

#### `Measurements/`
Performance and reliability analysis tools.
- **`bit_flip_analysis.py`**: Analyzes the impact of bit corruption on non-critical files
- **`jpeg_performance_test.py`**: Measures GuetzliSplit operation performance
- **`storage_overhead_analysis.py`**: Analyzes storage efficiency and overhead
- **`README.md`**: Comprehensive documentation for all measurement tools

#### `TestImages/`
Collection of test images for validation and performance testing.
- Contains various JPEG, PNG, and BMP images of different sizes
- Used by tests and measurement tools for comprehensive evaluation
- Includes both small and large files for stress testing

### Tools and Samples

#### Root Level Files
- **`BitFlipper.c`**: Tool for bit manipulation and corruption testing
- **`Makefile`**: Build configuration for the entire project
- **Sample files**: `high_res.jpg`, `jpeg_sample.jpg`, `sample_640×426.png`, `sample1.bmp`, `test_image.dng`

## Quick Start

### Prerequisites
- FUSE3 development libraries
- GCC compiler
- Python 3.6+ (for measurement tools)
- ImageMagick (for JPEG type detection in measurements)

### Build and Run
```bash
# Build the project
make

# Mount the filesystem (add flag -f for foreground running)
./CriticalFUSE ./mnt

# Run tests
make test

# Unmount when done
fusermount3 -u ./mnt
```

## Supported File Types

### ✅ Fully Supported
- **Text files** (`.txt`) - Complete support with critical/non-critical data separation
- **JPEG images** (`.jpg`, `.jpeg`) - Full support with GuetzliSplit integration
- **PNG images** (`.png`) - Supports partitioning but **cannot accept bit flips** due to CRC checking
- **BMP images** (`.bmp`) - Full support with critical/non-critical data separation

### ⚠️ Experimental/Not Supported
- **DNG files** (`.dng`) - Experimental implementation, not recommended for production use

## Build System

The project uses a comprehensive Makefile with the following targets:

### Main Targets
- `make` or `make all`: Builds all executables
- `make clean`: Removes all compiled objects and executables
- `make run`: Runs the HandlerTest executable
- `make run_fuse`: Runs the CriticalFUSE filesystem in foreground mode

### Individual Components
- `make CriticalFUSE`: Build only the FUSE filesystem
- `make HandlerTest`: Build only the handler test
- `make BitFlipper`: Build only the bit flipper tool

### Testing Targets
- `make test`: Run all test suites
- `make test_basic`: Run basic operations tests only
- `make test_file_types`: Run file type tests only
- `make test_stress`: Run stress tests only

## Testing

### Running Tests
```bash
# Run all tests
make test

# Run specific test suites
make test_basic
make test_file_types
make test_stress

# Run tests manually
./tests/run_all_tests.sh ./mnt ./storage
```

### Test Coverage
- Basic filesystem operations (create, read, write, delete, rename)
- File type handling for all supported formats
- Stress testing with large files and concurrent operations
- Edge cases and error conditions

## Performance Analysis

### Available Tools
```bash
# Bit flip analysis
python3 Measurements/bit_flip_analysis.py ./storage ./BitFlipper --output-dir ./results

# JPEG performance testing
python3 Measurements/jpeg_performance_test.py ./TestImages/ ./regular_test/ ./mnt/ --output-dir ./results

# Storage overhead analysis
python3 Measurements/storage_overhead_analysis.py ./TestImages ./mnt ./storage --output-dir ./results
```

### Analysis Capabilities
- Bit corruption impact analysis
- Performance benchmarking
- Storage efficiency measurement
- Comprehensive reporting and visualization

## Development

### Adding New File Handlers
1. Create new handler class inheriting from `AbstractFileHandler`
2. Implement required methods (especially `createMapping`)
3. Register handler in `CriticalFUSE.cpp`
4. Add to Makefile build system
5. Add tests for the new file type

### Code Organization
- **FUSE/**: Main filesystem implementation
- **FileHandlers/**: File type specific logic
- **Utilities/**: Shared utility functions
- **GuetzliSplit/**: JPEG compression library
- **tests/**: Test suite and validation
- **Measurements/**: Performance analysis tools

### Important Notes
- DNG and raw file handlers are experimental and should not be used as reference implementations
- PNG files cannot tolerate bit flips due to CRC validation
- JPEG files have full GuetzliSplit integration for advanced compression
- All file handlers must implement the critical/non-critical data separation pattern

## Troubleshooting

### Common Issues
1. **Mount point not found**: Ensure CriticalFUSE is properly mounted
2. **Permission denied**: Check file permissions and FUSE mount options
3. **Missing dependencies**: Install FUSE3 development libraries
4. **Test failures**: Verify test assets are present in `tests/test_assets/`

### Debug Mode
Run CriticalFUSE with debug output:
```bash
./CriticalFUSE -f -d ./mnt
```

This will show all FUSE operations in real-time for debugging purposes.

---

For more detailed information about specific components, refer to the README files in the `tests/` and `Measurements/` directories.
