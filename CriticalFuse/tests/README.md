# CriticalFUSE Test Suite

This directory contains comprehensive tests for the CriticalFUSE filesystem. The tests verify that the filesystem correctly handles file operations, critical file splitting, and various edge cases.

## Test Structure

### Test Scripts

1. **`test_basic_operations.sh`** - Tests basic filesystem operations
   - Directory listing
   - File creation, reading, writing
   - File renaming and deletion
   - Critical file handling

2. **`test_file_types.sh`** - Tests different file types
   - Text files (.txt)
   - JPEG files (.jpg, .jpeg)
   - PNG files (.png)
   - BMP files (.bmp)
   - DNG files (.dng)
   - Unsupported file types

3. **`test_stress.sh`** - Stress tests and edge cases
   - Large file operations
   - Concurrent file operations
   - Deep directory structures
   - Many small files
   - Special characters in filenames

4. **`run_all_tests.sh`** - Master test runner
   - Runs all test suites
   - Provides comprehensive results
   - Checks prerequisites

## Usage

### Prerequisites

1. Build CriticalFUSE:
   ```bash
   make clean
   make
   ```


2. Mount CriticalFUSE:
   ```bash
   # In one terminal
   make run_fuse
   # This will mount the filesystem at ./mnt
   ```

### Running Tests

#### Run All Tests
```bash
# Run all test suites
make test

# Or run directly
./tests/run_all_tests.sh ./mnt ./storage
```

#### Run Individual Test Suites
```bash
# Basic operations only
make test_basic

# File types only
make test_file_types

# Stress tests only
make test_stress

# Or run directly
./tests/test_basic_operations.sh ./mnt ./storage
./tests/test_file_types.sh ./mnt ./storage
./tests/test_stress.sh ./mnt ./storage
```

#### Run Tests on Custom Mount Point and Backing Directory
```bash
# If your filesystem is mounted elsewhere
./tests/run_all_tests.sh /path/to/your/mount/point /path/to/your/backing/dir
```

## Test Requirements

### Test Files
The tests use files from the `tests/test_assets/` directory:

- `office_100KB.jpg` - Small JPEG test file
- `elephant_400KB.jpeg` - Medium JPEG test file
- `tree_9200KB.jpg` - Large JPEG test file
- `sample_640×426.png` - PNG test file
- `sample1.bmp` - BMP test file  
- `test_image.dng` - DNG test file

These files are supposed to be in the test_assets folder

### System Requirements
- Bash shell
- Standard Unix utilities (ls, mkdir, cp, mv, rm, etc.)
- `file` command for file type detection
- `stat` command for file attributes

## Test Output

Each test provides:
- Individual test results (PASS/FAIL)
- Summary statistics
- Error messages for failed tests

Example output:
```
Testing CriticalFUSE at mount point: ./mnt
Backing directory: ./storage
================================================
Testing directory listing... PASS
Testing create directory... PASS
Testing create regular text file... PASS
...
================================================
Test Results:
  Passed: 20
  Failed: 0
  Total:  20
All tests passed! ✓
```

## Troubleshooting

### Common Issues

1. **"Mount point not found"**
   - Make sure CriticalFUSE is mounted
   - Check that the mount point directory exists

2. **"Permission denied"**
   - Make sure test scripts are executable: `chmod +x tests/*.sh`

3. **"Test files not found"**
   - Some tests will be skipped if test images are missing
   - This is normal and won't cause test failures

4. **FUSE filesystem not detected**
   - Make sure CriticalFUSE is properly mounted
   - Check with `mount | grep fuse`

### Debug Mode

To see detailed output from the filesystem, run CriticalFUSE in debug mode:
```bash
./CriticalFUSE -f -d ./mnt
```

This will show all FUSE operations in real-time, which can help debug test failures.

## Adding New Tests

To add new tests:

1. Create a new test script in the `tests/` directory
2. Follow the naming convention: `test_*.sh`
3. Use the `run_test()` function for consistent output
4. Add the new test to `run_all_tests.sh`
5. Update this README

Example test structure:
```bash
#!/bin/bash
set -e

MOUNT_POINT="$1"
BACKING_DIR="$2"

# ... validation ...

run_test() {
    local test_name="$1"
    local test_command="$2"
    
    echo -n "Testing $test_name... "
    
    if eval "$test_command" >/dev/null 2>&1; then
        echo "PASS"
        ((TESTS_PASSED++))
    else
        echo "FAIL"
        ((TESTS_FAILED++))
    fi
}

# Your tests here
run_test "my test" "command_to_test"

# Print results
echo "Results: $TESTS_PASSED passed, $TESTS_FAILED failed"
```
