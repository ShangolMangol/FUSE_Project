#!/bin/bash

# Basic FUSE operations test script
# Usage: ./test_basic_operations.sh /path/to/mnt /path/to/backing/dir

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

echo "Testing CriticalFUSE at mount point: $MOUNT_POINT"
echo "Backing directory: $BACKING_DIR"
echo "================================================"

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

# Test 1: Directory listing
run_test "directory listing" "ls -la '$MOUNT_POINT'"

# Test 2: Create a test directory
run_test "create directory" "mkdir -p '$MOUNT_POINT/test_dir'"

# Test 3: Create a regular text file
run_test "create regular text file" "echo 'Hello World' > '$MOUNT_POINT/test_dir/regular.txt'"

# Test 4: Read regular text file
run_test "read regular text file" "cat '$MOUNT_POINT/test_dir/regular.txt' | grep -q 'Hello World'"

# Test 5: Create a critical text file
run_test "create critical text file" "echo 'Critical content' > '$MOUNT_POINT/test_dir/critical.txt'"

# Test 6: Check if mapping file was created for critical file
run_test "mapping file creation" "[ -f '$BACKING_DIR/test_dir/critical.txt.mapping' ]"

# Test 7: Read critical text file
run_test "read critical text file" "cat '$MOUNT_POINT/test_dir/critical.txt' | grep -q 'Critical content'"

# Test 8: Create a JPEG file (copy from test images)
if [ -f "tests/test_assets/office_100KB.jpg" ]; then
    run_test "create critical JPEG file" "cp tests/test_assets/office_100KB.jpg '$MOUNT_POINT/test_dir/test.jpg'"
    
    # Test 9: Check if JPEG mapping file was created
    run_test "JPEG mapping file creation" "[ -f '$BACKING_DIR/test_dir/test.jpg.mapping' ]"
    
    # Test 10: Check if JPEG critical and non-critical files were created
    run_test "JPEG critical file creation" "[ -f '$BACKING_DIR/test_dir/test.jpg.crit' ]"
    run_test "JPEG non-critical file creation" "[ -f '$BACKING_DIR/test_dir/test.jpg.noncrit' ]"
    
    # Test 11: Read JPEG file
    run_test "read JPEG file" "file '$MOUNT_POINT/test_dir/test.jpg' | grep -q 'JPEG'"
else
    echo "Warning: tests/test_assets/office_100KB.jpg not found, skipping JPEG tests"
fi

# Test 12: File attributes
run_test "file attributes" "stat '$MOUNT_POINT/test_dir/critical.txt' >/dev/null"

# Test 13: File permissions
run_test "file permissions" "[ -r '$MOUNT_POINT/test_dir/critical.txt' ]"

# Test 14: Rename file
run_test "rename file" "mv '$MOUNT_POINT/test_dir/regular.txt' '$MOUNT_POINT/test_dir/renamed.txt'"

# Test 15: Verify rename worked
run_test "verify rename" "[ -f '$MOUNT_POINT/test_dir/renamed.txt' ] && [ ! -f '$MOUNT_POINT/test_dir/regular.txt' ]"

# Test 16: Delete file
run_test "delete file" "rm '$MOUNT_POINT/test_dir/renamed.txt'"

# Test 17: Verify deletion
run_test "verify deletion" "[ ! -f '$MOUNT_POINT/test_dir/renamed.txt' ]"

# Test 18: Delete critical file
run_test "delete critical file" "rm '$MOUNT_POINT/test_dir/critical.txt'"

# Test 19: Verify critical file deletion (including mapping files)
run_test "verify critical file deletion" "[ ! -f '$MOUNT_POINT/test_dir/critical.txt' ] && [ ! -f '$BACKING_DIR/test_dir/critical.txt.mapping' ]"

# Test 20: Clean up test directory
run_test "cleanup test directory" "rm -rf '$MOUNT_POINT/test_dir'"

echo "================================================"
echo "Test Results:"
echo "  Passed: $TESTS_PASSED"
echo "  Failed: $TESTS_FAILED"
echo "  Total:  $((TESTS_PASSED + TESTS_FAILED))"

if [ $TESTS_FAILED -eq 0 ]; then
    echo "All tests passed! ✓"
    exit 0
else
    echo "Some tests failed! ✗"
    exit 1
fi
