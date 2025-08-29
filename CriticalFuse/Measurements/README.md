# CriticalFUSE Measurements and Analysis Tools

This directory contains comprehensive measurement and analysis tools for the CriticalFUSE filesystem. These tools help evaluate performance, storage overhead, and reliability characteristics of the CriticalFUSE system.

## Table of Contents

- [Overview](#overview)
- [Available Tools](#available-tools)
  - [1. Bit Flip Analysis](#1-bit-flip-analysis)
  - [2. JPEG Performance Testing](#2-jpeg-performance-testing)
  - [3. Storage Overhead Analysis](#3-storage-overhead-analysis)
- [Prerequisites](#prerequisites)
- [Installation](#installation)
- [Quick Start](#quick-start)
- [Detailed Usage](#detailed-usage)
- [Output and Results](#output-and-results)
- [Common Use Cases](#common-use-cases)
- [Troubleshooting](#troubleshooting)
- [Technical Details](#technical-details)
- [Contributing](#contributing)

## Overview

The CriticalFUSE measurement suite provides three main analysis tools:

1. **Bit Flip Analysis** - Evaluates the impact of bit corruption on non-critical files
2. **JPEG Performance Testing** - Measures GuetzliSplit operation performance
3. **Storage Overhead Analysis** - Analyzes storage efficiency and overhead

Each tool generates comprehensive reports, graphs, and statistical analysis to help understand CriticalFUSE behavior under various conditions.

## Available Tools

### 1. Bit Flip Analysis

**Script:** `bit_flip_analysis.py`  
**Purpose:** Analyzes the impact of bit flips on non-critical files in the CriticalFUSE storage system

#### Key Features
- Applies increasing percentages of random bit flips to `.ac.noncrit` files
- Uses GuetzliSplit for image splitting and merging operations
- Calculates Structural Similarity Index (SSIM) between original and modified images
- Generates comprehensive graphs showing bit flip impact on image quality
- Supports both existing files and test image analysis
- Includes backup/restore functionality for failed operations

#### How It Works
1. **File Discovery**: Scans the storage folder for `.ac.noncrit` files
2. **Image Mapping**: For each `.ac.noncrit` file, finds the corresponding merged image
3. **Bit Flipping**: Uses the BitFlipper executable to apply random bit flips to `.ac.noncrit` files
4. **FUSE Integration**: Waits for FUSE to update and reads the modified merged images
5. **Quality Analysis**: Calculates SSIM between original and modified images
6. **Visualization**: Generates graphs showing the relationship between bit flip percentage and image quality

#### Command Line Arguments
- `storage_folder`: Path to storage folder containing `.ac.noncrit` files
- `bitflipper_path`: Path to BitFlipper executable
- `--output-dir, -o`: Path to output directory for results (required)
- `--test-images`: Path to folder containing test images to copy to mount point
- `--use-test-images`: Use test images instead of existing `.ac.noncrit` files
- `--max-test-images`: Maximum number of test images to analyze (default: all)
- `--guetzli-split`: Path to GuetzliSplit executable (default: `/usr/local/bin/GuetzliSplit`)
- `--output`: Output JSON file for results (optional)
- `--flip-range`: Range of bit flip percentages (min max) (default: 0.1 5.0)
- `--flip-steps`: Number of steps between min and max percentage (default: 20)
- `--no-cleanup`: Skip cleanup after testing

#### Example Usage
```bash
# Analyze existing .ac.noncrit files
python3 bit_flip_analysis.py ./storage ./BitFlipper --output-dir ./bitflip_results

# Use test images from TestImages folder
python3 bit_flip_analysis.py ./storage ./BitFlipper --output-dir ./bitflip_results \
    --test-images ./TestImages --use-test-images

# Use only first 5 test images
python3 bit_flip_analysis.py ./storage ./BitFlipper --output-dir ./bitflip_results \
    --test-images ./TestImages --use-test-images --max-test-images 5

# Custom flip range
python3 bit_flip_analysis.py ./storage ./BitFlipper -o ./results --flip-range 0.1 5.0
```

### 2. JPEG Performance Testing

**Script:** `jpeg_performance_test.py`  
**Purpose:** Measures the performance of GuetzliSplit operations for JPEG images

#### Key Features
- Automatic JPEG detection in specified folders (`.jpg`, `.jpeg`, `.JPG`, `.JPEG`)
- Tests image splitting, reading, and merging operations
- Measures split, read, and merge times with MB/s transfer rates
- Generates performance graphs with trend analysis
- Automatic cleanup with graph preservation
- JSON output for detailed results
- Progress tracking for each file being processed
- File display simulation during read operations to measure complete read time

#### Command Line Arguments
- `test_images_folder`: Path to folder containing test JPEG images (required)
- `regular_folder`: Path to regular folder for comparison (required)
- `mounted_folder`: Path to mounted FUSE folder for comparison (required)
- `--output-dir, -o`: Path to output directory for results (required)
- `--output`: Output JSON file for results (optional)
- `--no-cleanup`: Skip cleanup after testing (optional)
- `--no-display`: Skip file display simulation during read operations (optional)

#### Example Usage
```bash
# Basic usage with local output directory
python3 jpeg_performance_test.py ../TestImages/ ./regular_test/ ./mnt/ --output-dir ./performance_results

# Save results to JSON file
python3 jpeg_performance_test.py ../TestImages/ ./regular_test/ ./mnt/ -o ./results --output results.json

# Skip cleanup (for debugging)
python3 jpeg_performance_test.py ../TestImages/ ./regular_test/ ./mnt/ -o ./results --no-cleanup

# Skip display simulation
python3 jpeg_performance_test.py ../TestImages/ ./regular_test/ ./mnt/ -o ./results --no-display
```

### 3. Storage Overhead Analysis

**Script:** `storage_overhead_analysis.py`  
**Purpose:** Measures storage overhead caused by the FUSE system

#### Key Features
- Analyzes storage efficiency by comparing original vs. total storage used
- Detects JPEG types (baseline vs. progressive) using ImageMagick
- Generates comprehensive storage analysis graphs
- Provides file-type specific analysis
- Creates storage composition breakdowns
- Statistical analysis with mean, median, and distribution data
- Batch JPEG type detection for efficiency

#### Command Line Arguments
- `source_folder`: Path to folder containing original files (required)
- `mounted_folder`: Path to mounted FUSE folder (required)
- `storage_folder`: Path to storage folder where FUSE creates files (required)
- `--output-dir, -o`: Path to output directory for results (required)
- `--output`: Output JSON file for results (optional)
- `--max-files`: Maximum number of files to analyze (default: all)
- `--no-cleanup`: Skip cleanup after testing (optional)

#### Example Usage
```bash
# Basic usage
python3 storage_overhead_analysis.py ./TestImages ./mnt ./storage --output-dir ./overhead_results

# Limit to 10 files
python3 storage_overhead_analysis.py ./TestImages ./mnt ./storage -o ./results --max-files 10

# Save results to JSON
python3 storage_overhead_analysis.py ./TestImages ./mnt ./storage -o ./results --output results.json
```

## Prerequisites

### System Requirements
- Python 3.6+
- FUSE3 development libraries
- GCC compiler
- ImageMagick (for JPEG type detection)

### Python Dependencies
```bash
pip install opencv-python scikit-image matplotlib numpy
```

### CriticalFUSE Components
- CriticalFUSE filesystem mounted
- BitFlipper executable (for bit flip analysis)
- GuetzliSplit binary at `/usr/local/bin/GuetzliSplit`

## Installation

### 1. Install Python Dependencies
```bash
pip install opencv-python scikit-image matplotlib numpy
```

### 2. Install System Dependencies

#### Ubuntu/Debian
```bash
sudo apt-get install fuse3 libfuse3-dev imagemagick
```

#### macOS
```bash
brew install fuse3 imagemagick
```

#### Windows
- Install WSL2 with Ubuntu
- Follow Ubuntu instructions above

### 3. Build Required Components
```bash
cd CriticalFuse
make BitFlipper
```

### 4. Mount CriticalFUSE
```bash
./CriticalFUSE -f ./mnt
```

## Quick Start

### 1. Setup Environment
```bash
# Install Python dependencies
pip install opencv-python scikit-image matplotlib numpy

# Build required components
cd CriticalFuse
make BitFlipper

# Mount CriticalFUSE filesystem
./CriticalFUSE -f ./mnt
```

### 2. Run Bit Flip Analysis
```bash
python3 Measurements/bit_flip_analysis.py \
    ./storage \
    ./BitFlipper \
    --output-dir ./bitflip_results \
    --test-images ./TestImages \
    --use-test-images
```

### 3. Run Performance Testing
```bash
python3 Measurements/jpeg_performance_test.py \
    ./TestImages/ \
    ./regular_test/ \
    ./mnt/ \
    --output-dir ./performance_results
```

### 4. Run Storage Overhead Analysis
```bash
python3 Measurements/storage_overhead_analysis.py \
    ./TestImages \
    ./mnt \
    ./storage \
    --output-dir ./overhead_results
```

## Detailed Usage

### Bit Flip Analysis Workflow

1. **Prepare Test Environment**
   ```bash
   # Ensure CriticalFUSE is mounted
   ./CriticalFUSE -f ./mnt
   
   # Verify BitFlipper is built
   ls -la ./BitFlipper
   ```

2. **Run Analysis with Test Images**
   ```bash
   python3 bit_flip_analysis.py ./storage ./BitFlipper \
       --output-dir ./bitflip_results \
       --test-images ./TestImages \
       --use-test-images \
       --max-test-images 5 \
       --flip-range 0.1 3.0 \
       --flip-steps 15
   ```

3. **Analyze Existing Files**
   ```bash
   python3 bit_flip_analysis.py ./storage ./BitFlipper \
       --output-dir ./bitflip_results \
       --flip-range 0.1 5.0 \
       --flip-steps 20
   ```

### JPEG Performance Testing Workflow

1. **Prepare Test Environment**
   ```bash
   # Create regular test directory
   mkdir -p ./regular_test
   
   # Ensure mounted folder exists
   ls -la ./mnt/
   ```

2. **Run Performance Comparison**
   ```bash
   python3 jpeg_performance_test.py ./TestImages/ ./regular_test/ ./mnt/ \
       --output-dir ./performance_results \
       --output performance_results.json
   ```

3. **Run with Display Simulation Disabled**
   ```bash
   python3 jpeg_performance_test.py ./TestImages/ ./regular_test/ ./mnt/ \
       --output-dir ./performance_results \
       --no-display
   ```

### Storage Overhead Analysis Workflow

1. **Prepare Test Environment**
   ```bash
   # Ensure all directories exist
   ls -la ./TestImages/
   ls -la ./mnt/
   ls -la ./storage/
   ```

2. **Run Full Analysis**
   ```bash
   python3 storage_overhead_analysis.py ./TestImages ./mnt ./storage \
       --output-dir ./overhead_results \
       --output overhead_results.json
   ```

3. **Run Limited Analysis**
   ```bash
   python3 storage_overhead_analysis.py ./TestImages ./mnt ./storage \
       --output-dir ./overhead_results \
       --max-files 10
   ```

## Output and Results

### Bit Flip Analysis Output

The script generates:
- **JSON Results**: Detailed analysis data in JSON format
- **Combined Graphs**: Overview plots showing all files
- **Individual Graphs**: Per-file analysis plots
- **Statistical Summary**: Error bars and trend analysis

#### Graph Types
1. **SSIM vs Bit Flip Percentage by File**: Individual lines for each file
2. **Average SSIM vs Bit Flip Percentage**: Overall trend with trend line
3. **Statistical Summary**: Error bars showing standard deviation and min/max ranges

#### Output Structure
```
bitflip_results/
├── bitflip_results.json          # Detailed results
└── bitflip_results_graphs/       # Generated graphs
    ├── bit_flip_analysis.png     # Combined overview
    ├── bit_flip_statistical_summary.png
    ├── bit_flip_file1_noncrit.png
    ├── bit_flip_file2_noncrit.png
    └── ...
```

### JPEG Performance Testing Output

The script provides:
1. **Real-time Progress**: Shows which file is being processed and its performance metrics
2. **Summary Statistics**: Total files, size, time, and average speeds for all operations
3. **Detailed Metrics**: Split, read, and merge performance for each file
4. **Performance Graphs**: Scatter plots showing split/merge times vs file sizes with trend lines
5. **JSON Results** (optional): Detailed results including individual file metrics

#### Graph Output
- **`performance_analysis.png`**: Combined view with three subplots (split, merge, read times)
- **`split_time_analysis.png`**: Detailed split time analysis with file annotations
- **`merge_time_analysis.png`**: Detailed merge time analysis with file annotations  
- **`read_time_analysis.png`**: Detailed read time analysis with file annotations

#### Sample Output
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
```

### Storage Overhead Analysis Output

The script generates comprehensive storage analysis including:
- **Storage Efficiency Graphs**: Original vs. total storage usage
- **Overhead Percentage Analysis**: Storage overhead by file type
- **File Type Breakdown**: Analysis by file extension
- **JPEG Type Comparison**: Baseline vs. progressive JPEG analysis
- **Storage Composition Charts**: Breakdown of critical vs. non-critical storage
- **Statistical Summaries**: Mean, median, and distribution data

#### Output Structure
```
overhead_results/
├── overhead_results.json          # Detailed results
└── overhead_results_graphs/       # Generated graphs
    ├── storage_overhead_analysis.png
    ├── storage_efficiency.png
    ├── overhead_by_filetype.png
    ├── jpeg_type_comparison.png
    ├── stacked_storage_composition.png
    └── summary_statistics.txt
```

## Common Use Cases

### Performance Evaluation
- **Benchmarking:** Compare CriticalFUSE performance against regular filesystems
- **Optimization:** Identify bottlenecks in image processing workflows
- **Scalability:** Test performance with different file sizes and types

### Reliability Testing
- **Fault Tolerance:** Evaluate system behavior under bit corruption
- **Data Integrity:** Measure impact of storage errors on image quality
- **Recovery Analysis:** Test system resilience to various failure modes

### Storage Analysis
- **Efficiency Measurement:** Quantify storage overhead and waste
- **File Type Analysis:** Compare storage patterns across different image formats
- **Optimization Opportunities:** Identify areas for storage improvement

### Development and Testing
- **Regression Testing:** Ensure performance doesn't degrade with changes
- **Feature Validation:** Verify new functionality works as expected
- **Integration Testing:** Test complete workflows end-to-end

## Troubleshooting

### Common Issues

#### 1. Missing Dependencies
```bash
# Install missing Python packages
pip install opencv-python scikit-image matplotlib numpy

# Install ImageMagick (Ubuntu/Debian)
sudo apt-get install imagemagick

# Install ImageMagick (macOS)
brew install imagemagick
```

#### 2. CriticalFUSE Not Mounted
```bash
# Check if FUSE is mounted
ls -la ./mnt/

# Mount CriticalFUSE if not mounted
./CriticalFUSE -f ./mnt
```

#### 3. Missing Executables
```bash
# Build BitFlipper
make BitFlipper

# Check GuetzliSplit location
which GuetzliSplit
# Should return: /usr/local/bin/GuetzliSplit
```

#### 4. Permission Issues
```bash
# Ensure write permissions to output directories
chmod 755 ./output_directory

# Check FUSE mount permissions
ls -la ./mnt/
```

#### 5. No Test Files Found
```bash
# Verify test images exist
ls -la ./TestImages/

# Check file extensions (should include .jpg, .jpeg, .png, .bmp)
find ./TestImages/ -name "*.jpg" -o -name "*.jpeg" -o -name "*.png" -o -name "*.bmp"
```

#### 6. Bit Flip Analysis Specific Issues
- **Mount point not found**: Ensure FUSE is mounted with `./CriticalFUSE -f ./mnt`
- **BitFlipper not found**: Build it with `make BitFlipper`
- **No .ac.noncrit files**: Ensure you have split files in the storage directory
- **Python dependencies missing**: Install with `pip install opencv-python scikit-image matplotlib numpy`

#### 7. Performance Testing Specific Issues
- **Source folder validation**: Checks if the specified folder exists
- **GuetzliSplit validation**: Ensures GuetzliSplit binary is available
- **Permission errors**: Handles cases where output directory is not writable
- **Interruption handling**: Gracefully handles Ctrl+C interruptions

#### 8. Storage Analysis Specific Issues
- **ImageMagick not found**: Install ImageMagick for JPEG type detection
- **Storage folder not found**: Ensure the storage directory exists and contains FUSE files
- **No related files found**: Check that files have been written to the mounted FUSE folder

### Debug Mode
Most scripts support verbose output. Add debug flags or modify scripts to include more detailed logging:

```bash
# Example: Run with verbose output
python3 -v bit_flip_analysis.py [arguments]
```

### Getting Help
Each script provides help information:
```bash
python3 bit_flip_analysis.py --help
python3 jpeg_performance_test.py --help
python3 storage_overhead_analysis.py --help
```

## Technical Details

### Bit Flip Analysis Technical Details
- **Bit Flipping**: Uses random bit flipping with specified percentage
- **SSIM Calculation**: Uses scikit-image's structural similarity implementation
- **FUSE Integration**: Waits for filesystem updates after modifications
- **Image Processing**: Converts to grayscale for SSIM calculation
- **Error Handling**: Includes backup/restore functionality for failed operations

### Performance Considerations
- The script processes files sequentially to avoid FUSE conflicts
- Each bit flip operation includes a wait period for FUSE updates
- Large files may take longer to process
- Consider reducing `flip_steps` for faster analysis

### JPEG Performance Testing Technical Details
- **File Display Simulation**: Simulates display operations during read to measure complete read time
- **Performance Measurement**: Includes file system overhead and actual I/O operations
- **Graph Generation**: High resolution (300 DPI) for publication quality
- **Automatic Cleanup**: Removes test files after completion (graphs are preserved)

### Storage Overhead Analysis Technical Details
- **JPEG Type Detection**: Uses ImageMagick identify command for batch processing
- **Storage Composition**: Analyzes critical vs. non-critical vs. mapping file breakdown
- **Statistical Analysis**: Provides comprehensive statistics including mean, median, standard deviation
- **File Type Grouping**: Merges JPEG variants (.jpg and .jpeg) for consistent analysis

### Extending the Analysis

You can modify the scripts to:
- Add different quality metrics (PSNR, MSE, etc.)
- Analyze different file types
- Implement parallel processing
- Add more sophisticated error injection patterns
- Customize graph generation and statistical analysis

## Contributing

When adding new measurement tools:

1. Follow the existing naming convention: `[tool_name]_analysis.py`
2. Ensure consistent command-line interface patterns
3. Include comprehensive error handling and cleanup
4. Generate both JSON results and visualization graphs
5. Add detailed documentation to this README
6. Include help information with `--help` flag
7. Support common options like `--output-dir`, `--output`, `--no-cleanup`
8. Provide example usage and troubleshooting information

### Code Style Guidelines
- Use Python 3.6+ features
- Include comprehensive docstrings
- Follow PEP 8 style guidelines
- Include type hints where appropriate
- Add comprehensive error handling
- Use pathlib for file operations
- Include progress tracking for long operations

---

## License

This measurement suite is part of the CriticalFUSE project. See the main project license for details.