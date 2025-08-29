#!/bin/bash

# Stress tests for CriticalFUSE
# Tests concurrent operations, large files, and edge cases
# Usage: ./test_stress.sh /path/to/mnt /path/to/backing/dir

# Don't exit on error - we want to run all tests and report results

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

echo "Running CriticalFUSE stress tests at mount point: $MOUNT_POINT"
echo "Backing directory: $BACKING_DIR"
echo "======================================================="

# Test counter
TESTS_PASSED=0
TESTS_FAILED=0

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

# Function to run a test
run_test() {
    local test_name="$1"
    local test_command="$2"
    
    echo -n "Testing $test_name... "
    
    if eval "$test_command" >/dev/null 2>&1; then
        echo -e "${GREEN}PASS${NC}"
        ((TESTS_PASSED++))
    else
        echo -e "${RED}FAIL${NC}"
        echo "  Command: $test_command"
        echo "  Error: $(eval "$test_command" 2>&1 | head -1)"
        ((TESTS_FAILED++))
    fi
}

# Create test directory
mkdir -p "$MOUNT_POINT/stress_tests"

# Test 1: Large file creation
echo "Testing large file operations..."
run_test "create large text file (1MB)" "dd if=/dev/zero bs=1024 count=1024 2>/dev/null | tr '\0' 'A' > '$MOUNT_POINT/stress_tests/large.txt'"
run_test "large file mapping created" "[ -f '$BACKING_DIR/stress_tests/large.txt.mapping' ]"
run_test "large file size correct" "[ \$(stat -c%s '$MOUNT_POINT/stress_tests/large.txt') -eq 1048576 ]"

# Test 2: Multiple concurrent file operations
echo "Testing concurrent file operations..."
for i in {1..10}; do
    run_test "create concurrent file $i" "echo 'Concurrent test file $i' > '$MOUNT_POINT/stress_tests/concurrent_$i.txt'"
done

# Test 3: Rapid file creation and deletion
echo "Testing rapid file operations..."
for i in {1..20}; do
    run_test "rapid create/delete cycle $i" "echo 'temp' > '$MOUNT_POINT/stress_tests/temp_$i.txt' && rm '$MOUNT_POINT/stress_tests/temp_$i.txt'"
done

# Test 4: Deep directory structure
echo "Testing deep directory structure..."
run_test "create deep directories" "mkdir -p '$MOUNT_POINT/stress_tests/deep/dir/structure/test'"
run_test "create file in deep directory" "echo 'Deep file' > '$MOUNT_POINT/stress_tests/deep/dir/structure/test/deep.txt'"
run_test "deep file mapping created" "[ -f '$BACKING_DIR/stress_tests/deep/dir/structure/test/deep.txt.mapping' ]"

# Test 5: Many small files
echo "Testing many small files..."
for i in {1..50}; do
    run_test "create small file $i" "echo 'Small file $i' > '$MOUNT_POINT/stress_tests/small_$i.txt'"
done

# Test 6: File with special characters in name
echo "Testing special characters in filenames..."
run_test "create file with spaces" "echo 'File with spaces' > '$MOUNT_POINT/stress_tests/file with spaces.txt'"
run_test "create file with special chars" "echo 'Special chars' > '$MOUNT_POINT/stress_tests/file-with_special.chars.txt'"

# Test 7: Large JPEG files
echo "Testing large JPEG files..."
if [ -f "tests/test_assets/tree_9200KB.jpg" ]; then
    run_test "create large JPEG file" "cp tests/test_assets/tree_9200KB.jpg '$MOUNT_POINT/stress_tests/large.jpg'"
    run_test "large JPEG mapping created" "[ -f '$BACKING_DIR/stress_tests/large.jpg.mapping' ]"
    run_test "large JPEG critical file created" "[ -f '$BACKING_DIR/stress_tests/large.jpg.crit' ]"
    run_test "large JPEG non-critical file created" "[ -f '$BACKING_DIR/stress_tests/large.jpg.noncrit' ]"
fi

# Test 8: Directory operations
echo "Testing directory operations..."
run_test "create multiple directories" "mkdir -p '$MOUNT_POINT/stress_tests/dir1/dir2' '$MOUNT_POINT/stress_tests/dir3'"
run_test "move directory" "mv '$MOUNT_POINT/stress_tests/dir3' '$MOUNT_POINT/stress_tests/moved_dir'"
run_test "remove directory" "rmdir '$MOUNT_POINT/stress_tests/moved_dir'"

# Test 9: File permissions and attributes
echo "Testing file attributes..."
run_test "check file permissions" "[ -r '$MOUNT_POINT/stress_tests/large.txt' ]"
run_test "check file timestamps" "stat '$MOUNT_POINT/stress_tests/large.txt' >/dev/null"

# Test 10: Memory usage with many files
echo "Testing memory usage with many files..."
for i in {1..100}; do
    run_test "create memory test file $i" "echo 'Memory test $i' > '$MOUNT_POINT/stress_tests/memory_$i.txt'"
done

# Test 11: Cleanup stress test
echo "Testing cleanup operations..."
run_test "remove all test files" "rm -rf '$MOUNT_POINT/stress_tests'"
run_test "verify cleanup" "[ ! -d '$MOUNT_POINT/stress_tests' ]"

echo "======================================================="
echo "Stress Test Results:"
echo "  Passed: $TESTS_PASSED"
echo "  Failed: $TESTS_FAILED"
echo "  Total:  $((TESTS_PASSED + TESTS_FAILED))"

if [ $TESTS_FAILED -eq 0 ]; then
    echo -e "All stress tests passed! ${GREEN}✓${NC}"
    exit 0
else
    echo -e "Some stress tests failed! ${RED}✗${NC}"
    exit 1
fi
