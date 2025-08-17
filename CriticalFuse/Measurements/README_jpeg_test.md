# GuetzliSplit Performance Testing Script

This Python script measures the performance of GuetzliSplit operations for JPEG images, including splitting, reading, and merging operations. It includes automatic cleanup functionality.

## Features

- **Automatic JPEG Detection**: Finds all JPEG files (`.jpg`, `.jpeg`, `.JPG`, `.JPEG`) in the specified folder
- **GuetzliSplit Integration**: Tests image splitting and merging operations using GuetzliSplit
- **Performance Measurement**: Measures split, read, and merge times for each file
- **Speed Calculation**: Calculates MB/s transfer rates for all operations
- **Automatic Cleanup**: Removes test files after completion
- **JSON Output**: Optionally saves detailed results to a JSON file
- **Progress Tracking**: Shows progress for each file being processed
- **Flexible Output Directory**: Accepts custom output directory path via command line

## Requirements

- Python 3.6+
- Standard library modules (no external dependencies)
- Write permissions to the specified output directory
- GuetzliSplit binary at `/usr/local/bin/GuetzliSplit` (required)

## Usage

### Basic Usage

```bash
python3 jpeg_performance_test.py <source_folder> --output-dir <output_directory>
```

### Examples

```bash
# Basic usage with local output directory
python3 jpeg_performance_test.py ../TestImages/ --output-dir ./guetzli_test

# Save results to JSON file
python3 jpeg_performance_test.py ../TestImages/ -o ./guetzli_test --output results.json

# Skip cleanup (for debugging)
python3 jpeg_performance_test.py ../TestImages/ -o ./guetzli_test --no-cleanup
```

### Command Line Arguments

- `source_folder`: Path to folder containing JPEG images (required)
- `--output-dir, -o`: Path to output directory for GuetzliSplit operations (required)
- `--output`: Output JSON file for results (optional)
- `--no-cleanup`: Skip cleanup after testing (optional)

## Output

The script provides:

1. **Real-time Progress**: Shows which file is being processed and its performance metrics
2. **Summary Statistics**: Total files, size, time, and average speeds for all operations
3. **Detailed Metrics**: Split, read, and merge performance for each file
4. **JSON Results** (optional): Detailed results including individual file metrics

### Sample Output

```
=== GuetzliSplit Performance Testing ===
Source folder: ../TestImages/
Output directory: ./guetzli_test
GuetzliSplit path: /usr/local/bin/GuetzliSplit
Found 15 JPEG files in ../TestImages/

=== Testing GuetzliSplit Operations ===
Processing 1/15: image1.jpg
  Split: 0.1234s (8.45 MB/s)
  Read:  0.0234s (45.67 MB/s)
  Merge: 0.0567s (18.34 MB/s)
...

GuetzliSplit Operations Summary:
  Total files: 15
  Total size: 245.67 MB
  Total split time: 8.2345s
  Total read time: 1.5678s
  Total merge time: 3.4567s
  Avg split speed: 29.87 MB/s
  Avg read speed: 156.78 MB/s
  Avg merge speed: 71.12 MB/s

=== Cleaning up ===
Removed ./guetzli_test

=== Testing Complete ===
```

## JSON Output Format

If you specify an output file, the script saves detailed results in JSON format:

```json
{
  "timestamp": "2024-01-15T10:30:45.123456",
  "source_folder": "../TestImages/",
  "output_dir": "./guetzli_test",
  "guetzli_path": "/usr/local/bin/GuetzliSplit",
  "tests": [
    {
      "directory": "./guetzli_test",
      "test_name": "GuetzliSplit Operations",
      "files": [
        {
          "filename": "image1.jpg",
          "original_size": 2048576,
          "split_size": 2048576,
          "merge_size": 2048576,
          "split_time": 0.1234,
          "read_time": 0.0234,
          "merge_time": 0.0567,
          "split_speed_mbps": 16.23,
          "read_speed_mbps": 85.67,
          "merge_speed_mbps": 35.45
        }
      ],
      "summary": {
        "total_files": 15,
        "total_size_mb": 245.67,
        "total_split_time": 8.2345,
        "total_read_time": 1.5678,
        "total_merge_time": 3.4567,
        "avg_split_speed_mbps": 29.87,
        "avg_read_speed_mbps": 156.78,
        "avg_merge_speed_mbps": 71.12
      }
    }
  ]
}
```

## Error Handling

The script includes comprehensive error handling:

- **Source folder validation**: Checks if the specified folder exists
- **GuetzliSplit validation**: Ensures GuetzliSplit binary is available
- **Permission errors**: Handles cases where output directory is not writable
- **Interruption handling**: Gracefully handles Ctrl+C interruptions
- **Cleanup on errors**: Ensures test directories are cleaned up even if errors occur

## Notes

- The script creates the specified output directory if it doesn't exist
- All test files are automatically removed after testing (unless `--no-cleanup` is used)
- The script preserves original file metadata during operations
- Performance measurements include file system overhead and actual I/O operations
- You can use any path for the output directory (system paths, relative paths, etc.)

## Use Cases

- **GuetzliSplit Performance Analysis**: Evaluate the performance of image splitting and merging operations
- **Image Processing Benchmarking**: Measure the efficiency of specialized image processing tools
- **Development Testing**: Test GuetzliSplit performance in development environments
- **Performance Optimization**: Identify bottlenecks in image splitting workflows
