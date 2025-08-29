#!/bin/bash

# File type specific tests for CriticalFUSE
# Tests different file types (TXT, JPEG, PNG, BMP, DNG)
# Usage: ./test_file_types.sh /path/to/mnt /path/to/backing/dir

set -e  # Exit on any error

MOUNT_POINT="$1"
BACKING_DIR="$2"

if [ -z "$MOUNT_POINT" ] || [ -z "$BACKING_DIR" ]; then
    echo "Usage: $0 <mount_point> <backing_directory>"
    echo "Example: $0 ./mnt ./storage"
    exit 1
fi

if [ ! -d "$MOUNT_POINT" ]; then
    echo "Error: Mount point '$MOUNT_POINT' does not exist or is not a directory"
    exit 1
fi

if [ ! -d "$BACKING_DIR" ]; then
    echo "Error: Backing directory '$BACKING_DIR' does not exist or is not a directory"
    exit 1
fi

echo "Testing CriticalFUSE file types at mount point: $MOUNT_POINT"
echo "Backing directory: $BACKING_DIR"
echo "========================================================"

# Test counter
TESTS_PASSED=0
TESTS_FAILED=0

# Function to run a test
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

# Create test directory
mkdir -p "$MOUNT_POINT/file_type_tests"

# Test 1: Text file (.txt)
echo "Testing TEXT files..."
run_test "create .txt file" "echo 'This is a test text file' > '$MOUNT_POINT/file_type_tests/test.txt'"
run_test "txt mapping file created" "[ -f '$BACKING_DIR/file_type_tests/test.txt.mapping' ]"
run_test "read .txt file" "cat '$MOUNT_POINT/file_type_tests/test.txt' | grep -q 'test text file'"

# Test 2: JPEG files (.jpg, .jpeg)
echo "Testing JPEG files..."
if [ -f "tests/test_assets/office_100KB.jpg" ]; then
    run_test "create .jpg file" "cp tests/test_assets/office_100KB.jpg '$MOUNT_POINT/file_type_tests/test.jpg'"
    run_test "jpg mapping file created" "[ -f '$BACKING_DIR/file_type_tests/test.jpg.mapping' ]"
    run_test "jpg critical file created" "[ -f '$BACKING_DIR/file_type_tests/test.jpg.crit' ]"
    run_test "jpg non-critical file created" "[ -f '$BACKING_DIR/file_type_tests/test.jpg.noncrit' ]"
    run_test "read .jpg file" "file '$MOUNT_POINT/file_type_tests/test.jpg' | grep -q 'JPEG'"
fi

if [ -f "tests/test_assets/elephant_400KB.jpeg" ]; then
    run_test "create .jpeg file" "cp tests/test_assets/elephant_400KB.jpeg '$MOUNT_POINT/file_type_tests/test.jpeg'"
    run_test "jpeg mapping file created" "[ -f '$BACKING_DIR/file_type_tests/test.jpeg.mapping' ]"
    run_test "jpeg critical file created" "[ -f '$BACKING_DIR/file_type_tests/test.jpeg.crit' ]"
    run_test "jpeg non-critical file created" "[ -f '$BACKING_DIR/file_type_tests/test.jpeg.noncrit' ]"
    run_test "read .jpeg file" "file '$MOUNT_POINT/file_type_tests/test.jpeg' | grep -q 'JPEG'"
fi

# Test 3: PNG files (.png)
echo "Testing PNG files..."
if [ -f "tests/test_assets/sample_640×426.png" ]; then
    run_test "create .png file" "cp tests/test_assets/sample_640×426.png '$MOUNT_POINT/file_type_tests/test.png'"
    run_test "png mapping file created" "[ -f '$BACKING_DIR/file_type_tests/test.png.mapping' ]"
    run_test "read .png file" "file '$MOUNT_POINT/file_type_tests/test.png' | grep -q 'PNG'"
fi

# Test 4: BMP files (.bmp)
echo "Testing BMP files..."
if [ -f "tests/test_assets/sample1.bmp" ]; then
    run_test "create .bmp file" "cp tests/test_assets/sample1.bmp '$MOUNT_POINT/file_type_tests/test.bmp'"
    run_test "bmp mapping file created" "[ -f '$BACKING_DIR/file_type_tests/test.bmp.mapping' ]"
    run_test "read .bmp file" "file '$MOUNT_POINT/file_type_tests/test.bmp' | grep -q 'PC bitmap'"
fi

# Test 5: DNG files (.dng)
echo "Testing DNG files..."
if [ -f "tests/test_assets/test_image.dng" ]; then
    run_test "create .dng file" "cp tests/test_assets/test_image.dng '$MOUNT_POINT/file_type_tests/test.dng'"
    run_test "dng mapping file created" "[ -f '$BACKING_DIR/file_type_tests/test.dng.mapping' ]"
    run_test "read .dng file" "file '$MOUNT_POINT/file_type_tests/test.dng' | grep -q 'TIFF'"
fi

# Test 6: Unsupported file types (should be treated as regular files)
echo "Testing unsupported file types..."
run_test "create .pdf file (unsupported)" "echo 'PDF content' > '$MOUNT_POINT/file_type_tests/test.pdf'"
run_test "pdf file created (no mapping)" "[ -f '$MOUNT_POINT/file_type_tests/test.pdf' ] && [ ! -f '$BACKING_DIR/file_type_tests/test.pdf.mapping' ]"

# Test 7: File size verification
echo "Testing file size consistency..."
if [ -f "tests/test_assets/office_100KB.jpg" ]; then
    ORIGINAL_SIZE=$(stat -c%s "tests/test_assets/office_100KB.jpg")
    MOUNTED_SIZE=$(stat -c%s "$MOUNT_POINT/file_type_tests/test.jpg")
    run_test "JPEG file size consistency" "[ $ORIGINAL_SIZE -eq $MOUNTED_SIZE ]"
fi

# Test 8: Multiple files of same type
echo "Testing multiple files of same type..."
run_test "create second .txt file" "echo 'Second text file' > '$MOUNT_POINT/file_type_tests/test2.txt'"
run_test "second txt mapping created" "[ -f '$BACKING_DIR/file_type_tests/test2.txt.mapping' ]"

# Test 9: File operations on critical files
echo "Testing file operations on critical files..."
run_test "copy critical file" "cp '$MOUNT_POINT/file_type_tests/test.txt' '$MOUNT_POINT/file_type_tests/copied.txt'"
run_test "copied file mapping created" "[ -f '$BACKING_DIR/file_type_tests/copied.txt.mapping' ]"
run_test "move critical file" "mv '$MOUNT_POINT/file_type_tests/copied.txt' '$MOUNT_POINT/file_type_tests/moved.txt'"
run_test "moved file exists" "[ -f '$MOUNT_POINT/file_type_tests/moved.txt' ]"
run_test "moved file mapping exists" "[ -f '$BACKING_DIR/file_type_tests/moved.txt.mapping' ]"

# Test 10: Cleanup
echo "Cleaning up test files..."
run_test "cleanup test directory" "rm -rf '$MOUNT_POINT/file_type_tests'"

echo "========================================================"
echo "File Type Test Results:"
echo "  Passed: $TESTS_PASSED"
echo "  Failed: $TESTS_FAILED"
echo "  Total:  $((TESTS_PASSED + TESTS_FAILED))"

if [ $TESTS_FAILED -eq 0 ]; then
    echo "All file type tests passed! ✓"
    exit 0
else
    echo "Some file type tests failed! ✗"
    exit 1
fi
