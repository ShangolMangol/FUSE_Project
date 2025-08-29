#!/bin/bash

# Master test runner for CriticalFUSE
# Runs all test suites and provides comprehensive results
# Usage: ./run_all_tests.sh /path/to/mnt /path/to/backing/dir

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

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "CriticalFUSE Comprehensive Test Suite"
echo "===================================="
echo "Mount point: $MOUNT_POINT"
echo "Backing directory: $BACKING_DIR"
echo "Test directory: $SCRIPT_DIR"
echo ""

# Test results tracking
TOTAL_TESTS=0
TOTAL_PASSED=0
TOTAL_FAILED=0
FAILED_SUITES=()

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

# Function to run a test suite
run_test_suite() {
    local suite_name="$1"
    local suite_script="$2"
    
    echo "Running $suite_name..."
    echo "----------------------------------------"
    
    if [ ! -f "$suite_script" ]; then
        echo "Error: Test script '$suite_script' not found!"
        FAILED_SUITES+=("$suite_name (script not found)")
        return 1
    fi
    
    # Make sure the script is executable
    chmod +x "$suite_script"
    
    # Run the test suite and capture results
    if "$suite_script" "$MOUNT_POINT" "$BACKING_DIR"; then
        echo -e "$suite_name: ${GREEN}PASSED ✓${NC}"
        echo ""
        return 0
    else
        echo -e "$suite_name: ${RED}FAILED ✗${NC}"
        echo ""
        FAILED_SUITES+=("$suite_name")
        return 1
    fi
}

# Function to check if FUSE filesystem is mounted
check_fuse_mount() {
    echo "Checking FUSE filesystem status..."
    
    # Check if mount point exists and is accessible
    if [ ! -d "$MOUNT_POINT" ]; then
        echo "Error: Mount point '$MOUNT_POINT' does not exist"
        echo "Make sure CriticalFUSE is mounted before running tests"
        exit 1
    fi
    
    # Try to list the directory to see if it's accessible
    if ! ls "$MOUNT_POINT" >/dev/null 2>&1; then
        echo "Error: Cannot access mount point '$MOUNT_POINT'"
        echo "Make sure CriticalFUSE is mounted and accessible"
        exit 1
    fi
    
    echo "Mount point accessible at $MOUNT_POINT ✓"
    echo ""
}


# Function to check test prerequisites
check_prerequisites() {
    echo "Checking test prerequisites..."
    
    local missing_files=()
    
    # Check for test assets
    if [ ! -d "tests/test_assets" ]; then
        missing_files+=("tests/test_assets directory")
    fi
    
    if [ ! -f "tests/test_assets/office_100KB.jpg" ]; then
        missing_files+=("tests/test_assets/office_100KB.jpg")
    fi
    
    if [ ! -f "tests/test_assets/sample_640×426.png" ]; then
        missing_files+=("tests/test_assets/sample_640×426.png")
    fi
    
    if [ ! -f "tests/test_assets/sample1.bmp" ]; then
        missing_files+=("tests/test_assets/sample1.bmp")
    fi
    
    if [ ! -f "tests/test_assets/test_image.dng" ]; then
        missing_files+=("tests/test_assets/test_image.dng")
    fi
    
    if [ ${#missing_files[@]} -gt 0 ]; then
        echo "Warning: Missing test files:"
        for file in "${missing_files[@]}"; do
            echo "  - $file"
        done
        echo "Some tests may be skipped"
        echo ""
    else
        echo "All test prerequisites found ✓"
        echo ""
    fi
}

# Main test execution
main() {
    # Check prerequisites
    check_prerequisites
    
    # Check FUSE mount
    check_fuse_mount
    
    # Run test suites
    echo "Starting test execution..."
    echo ""
    
    # Test suite 1: Basic operations
    if run_test_suite "Basic Operations Test" "$SCRIPT_DIR/test_basic_operations.sh"; then
        ((TOTAL_PASSED++))
    else
        ((TOTAL_FAILED++))
    fi
    ((TOTAL_TESTS++))
    
    # Test suite 2: File types
    if run_test_suite "File Types Test" "$SCRIPT_DIR/test_file_types.sh"; then
        ((TOTAL_PASSED++))
    else
        ((TOTAL_FAILED++))
    fi
    ((TOTAL_TESTS++))
    
    # Test suite 3: Stress tests
    if run_test_suite "Stress Test" "$SCRIPT_DIR/test_stress.sh"; then
        ((TOTAL_PASSED++))
    else
        ((TOTAL_FAILED++))
    fi
    ((TOTAL_TESTS++))
    
    # Print final results
    echo "===================================="
    echo "FINAL TEST RESULTS"
    echo "===================================="
    echo "Test Suites:"
    echo "  Passed: $TOTAL_PASSED"
    echo "  Failed: $TOTAL_FAILED"
    echo "  Total:  $TOTAL_TESTS"
    echo ""
    
    if [ ${#FAILED_SUITES[@]} -gt 0 ]; then
        echo "Failed Test Suites:"
        for suite in "${FAILED_SUITES[@]}"; do
            echo "  - $suite"
        done
        echo ""
    fi
    
    if [ $TOTAL_FAILED -eq 0 ]; then
        echo -e "🎉 ${GREEN}ALL TESTS PASSED! CriticalFUSE is working correctly!${NC} 🎉"
        exit 0
    else
        echo -e "❌ ${RED}Some tests failed. Please check the output above for details.${NC}"
        exit 1
    fi
}

# Run main function
main "$@"
