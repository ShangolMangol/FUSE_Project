# JPEG Performance Testing Script

This Python script measures the performance of writing and reading JPEG images from specified mnt and storage directories, with automatic cleanup functionality.

## Features

- **Automatic JPEG Detection**: Finds all JPEG files (`.jpg`, `.jpeg`, `.JPG`, `.JPEG`) in the specified folder
- **Performance Measurement**: Measures write and read times for each file
- **Speed Calculation**: Calculates MB/s transfer rates
- **Comparison**: Compares performance between specified mnt and storage directories
- **Automatic Cleanup**: Removes test files after completion
- **JSON Output**: Optionally saves detailed results to a JSON file
- **Progress Tracking**: Shows progress for each file being processed
- **Flexible Paths**: Accepts custom mnt and storage directory paths via command line

## Requirements

- Python 3.6+
- Standard library modules (no external dependencies)
- Write permissions to the specified mnt and storage directories

## Usage

### Basic Usage

```bash
python3 jpeg_performance_test.py <source_folder> --mnt <mnt_path> --storage <storage_path>
```

### Examples

```bash
# Using system directories (requires appropriate permissions)
python3 jpeg_performance_test.py ../TestImages/ --mnt /mnt/test --storage /storage/test

# Using local directories (no special permissions needed)
python3 jpeg_performance_test.py ../TestImages/ --mnt ./mnt_test --storage ./storage_test

# Save results to JSON file
python3 jpeg_performance_test.py ../TestImages/ -m ./mnt_test -s ./storage_test --output results.json

# Skip cleanup (for debugging)
python3 jpeg_performance_test.py ../TestImages/ -m ./mnt_test -s ./storage_test --no-cleanup
```

### Command Line Arguments

- `source_folder`: Path to folder containing JPEG images (required)
- `--mnt, -m`: Path to mnt test directory (required)
- `--storage, -s`: Path to storage test directory (required)
- `--output, -o`: Output JSON file for results (optional)
- `--no-cleanup`: Skip cleanup after testing (optional)

## Output

The script provides:

1. **Real-time Progress**: Shows which file is being processed and its performance metrics
2. **Summary Statistics**: Total files, size, time, and average speeds for each directory
3. **Performance Comparison**: Side-by-side comparison between mnt and storage directories
4. **JSON Results** (optional): Detailed results including individual file metrics

### Sample Output

```
=== JPEG Performance Testing ===
Source folder: ../TestImages/
MNT path: ./mnt_test
Storage path: ./storage_test
Found 15 JPEG files in ../TestImages/

=== Testing MNT Test Directory ===
Processing 1/15: image1.jpg
  Write: 0.0234s (45.67 MB/s)
  Read:  0.0156s (68.45 MB/s)
...

MNT Test Directory Summary:
  Total files: 15
  Total size: 245.67 MB
  Total write time: 2.3456s
  Total read time: 1.5678s
  Avg write speed: 104.67 MB/s
  Avg read speed: 156.78 MB/s

=== Testing Storage Test Directory ===
...

=== Performance Comparison ===
Metric               MNT Test        Storage Test     Difference
-----------------------------------------------------------------
Write Speed (MB/s)   104.67          98.45           -5.9%
Read Speed (MB/s)    156.78          142.34          -9.2%
Total Time (s)       3.9134          4.1234          +5.4%

=== Cleaning up ===
Removed ./mnt_test
Removed ./storage_test

=== Testing Complete ===
```

## JSON Output Format

If you specify an output file, the script saves detailed results in JSON format:

```json
{
  "timestamp": "2024-01-15T10:30:45.123456",
  "source_folder": "../TestImages/",
  "mnt_path": "./mnt_test",
  "storage_path": "./storage_test",
  "tests": [
    {
      "directory": "./mnt_test",
      "test_name": "MNT Test Directory",
      "files": [
        {
          "filename": "image1.jpg",
          "original_size": 2048576,
          "copied_size": 2048576,
          "write_time": 0.0234,
          "read_time": 0.0156,
          "write_speed_mbps": 85.67,
          "read_speed_mbps": 128.45
        }
      ],
      "summary": {
        "total_files": 15,
        "total_size_mb": 245.67,
        "total_write_time": 2.3456,
        "total_read_time": 1.5678,
        "avg_write_speed_mbps": 104.67,
        "avg_read_speed_mbps": 156.78
      }
    }
  ]
}
```

## Error Handling

The script includes comprehensive error handling:

- **Source folder validation**: Checks if the specified folder exists
- **Permission errors**: Handles cases where mnt or storage directories are not writable
- **Interruption handling**: Gracefully handles Ctrl+C interruptions
- **Cleanup on errors**: Ensures test directories are cleaned up even if errors occur

## Notes

- The script creates the specified mnt and storage directories if they don't exist
- All test files are automatically removed after testing (unless `--no-cleanup` is used)
- The script preserves original file metadata during copying
- Performance measurements include file system overhead and actual I/O operations
- You can use any paths for mnt and storage directories (system paths, relative paths, etc.)

## Use Cases

- **System Performance Testing**: Compare performance between different mount points
- **Storage Device Comparison**: Test different storage devices or file systems
- **Network Storage Testing**: Compare local vs network storage performance
- **Development Testing**: Test performance in development environments
