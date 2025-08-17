<p align="center"><img src="https://cloud.githubusercontent.com/assets/203457/24553916/1f3f88b6-162c-11e7-990a-731b2560f15c.png" alt="Guetzli" width="64"></p>

# GuetzliSplit

GuetzliSplit is a fork of Guetzli with added support for splitting and merging JPEG files into critical and non-critical parts.

## Overview

GuetzliSplit extends the original Guetzli JPEG compressor with the ability to split JPEG files into two components:
- **Critical file** (`.jpg.crit`): Contains all JPEG structure, metadata, quantization tables, DC coefficients, and AC Huffman symbols
- **Non-critical file** (`.jpg.ac.noncrit`): Contains only the AC coefficient values

This separation allows for progressive loading, selective compression, or other applications where you want to handle the basic image structure separately from the fine detail data.

## Building and Running

### Build Instructions

1. **Install dependencies:**
   - You need a C++ compiler (e.g., g++, clang++) and `make`.
   - On Ubuntu/Debian: `sudo apt-get install build-essential libpng-dev`
   - On Windows: Use MSVC or MinGW, or build in WSL.

2. **Build the project:**
   ```sh
   cd guetzli-master
   make
   ```
   - The binary will be created as `guetzli` in the current directory.

### Running GuetzliSplit

#### Split a JPEG

```sh
./guetzli --split input.jpg output.jpg
```

This produces:
- `output.jpg.crit`: Critical JPEG data (structure, DC coefficients, AC symbols)
- `output.jpg.ac.noncrit`: AC coefficient values only

The critical file is a valid JPEG that can be displayed but will appear blurry since it lacks the AC coefficient values that provide fine detail.

#### Merge a Split JPEG

```sh
./guetzli --merge output.jpg.crit merged.jpg
```

This automatically looks for the corresponding `output.jpg.ac.noncrit` file and merges them to produce `merged.jpg`, which is identical to the original input.

#### Standard Compression (Original Guetzli)

```sh
./guetzli input.jpg output.jpg
```

Compresses `input.jpg` to `output.jpg` using Guetzli's standard algorithm.

#### Additional Options

```sh
./guetzli [--verbose] [--quality Q] [--memlimit M] [--split|--merge] input.jpg output.jpg
```

- `--verbose`: Print detailed processing information
- `--quality Q`: Set JPEG quality (default: 95)
- `--memlimit M`: Set memory limit in MB (default: 6000)
- `--nomemlimit`: Disable memory limits

## Technical Details

### Split/Merge Process

1. **Split Mode:**
   - Reads the input JPEG and parses its structure
   - Writes DC coefficients and AC Huffman symbols to the critical file
   - Writes AC coefficient values to the non-critical file
   - Both files are written simultaneously during the encoding process

2. **Merge Mode:**
   - Reads the critical file and parses its JPEG structure
   - Reads AC coefficient values from the non-critical file
   - Reconstructs the original JPEG by combining the data
   - Writes the complete JPEG to the output file

### File Structure

- **`.jpg.crit`**: Contains complete JPEG structure including:
  - JPEG markers and headers
  - Quantization tables
  - Huffman tables
  - DC coefficients
  - AC Huffman symbols (but with zeroed coefficient values)

- **`.jpg.ac.noncrit`**: Contains only the raw AC coefficient values as a bit stream

### Compatibility

- Input JPEGs must be baseline (not progressive)
- The split/merge process is lossless for baseline JPEGs
- All quantization tables, component IDs, and structure are preserved
- Only the AC coefficient values are separated

## Examples

### Basic Split and Merge

```sh
# Split a JPEG
./guetzli --split photo.jpg result.jpg

# This creates:
# - result.jpg.crit (critical data)
# - result.jpg.ac.noncrit (AC coefficients)

# Merge back to original
./guetzli --merge result.jpg.crit restored.jpg
```

### Quality Control

```sh
# Split with specific quality
./guetzli --quality 90 --split input.jpg output.jpg

# Merge with verbose output
./guetzli --verbose --merge output.jpg.crit final.jpg
```

## Use Cases

1. **Progressive Loading**: Load the critical file first for a basic image, then load the non-critical file for full detail
2. **Selective Compression**: Apply different compression strategies to critical vs non-critical data
3. **Bandwidth Optimization**: Prioritize critical data for low-bandwidth scenarios
4. **Storage Optimization**: Store critical data locally and non-critical data remotely

## Notes

- The split/merge process is **lossless** for baseline JPEGs
- Only AC coefficient values are separated; all other JPEG data remains in the critical file
- Input JPEGs must be baseline (not progressive)
- The critical file can be displayed as a JPEG but will appear blurry
- File extensions are automatically handled based on the base filename

## License

This project is based on [Guetzli by Google](https://github.com/google/guetzli) and is licensed under the Apache License, Version 2.0.
