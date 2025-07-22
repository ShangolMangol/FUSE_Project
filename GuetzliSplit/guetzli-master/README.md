<p align="center"><img src="https://cloud.githubusercontent.com/assets/203457/24553916/1f3f88b6-162c-11e7-990a-731b2560f15c.png" alt="Guetzli" width="64"></p>

# GuetzliSplit

GuetzliSplit is a fork of Guetzli with added support for splitting and merging JPEG files into critical and non-critical parts.

## Building and Running

### Build Instructions

1. **Install dependencies:**
   - You need a C++ compiler (e.g., g++, clang++) and `make`.
   - On Ubuntu/Debian: `sudo apt-get install build-essential`
   - On Windows: Use MSVC or MinGW, or build in WSL.

2. **Build the project:**
   ```sh
   cd guetzli-master
   make
   ```
   - The binaries will be created in `bin/Release/` (e.g., `bin/Release/guetzli`).

### Running GuetzliSplit

#### Split a JPEG

```
./bin/Release/guetzli --split input.jpg output.jpg
```
- Produces `output.crit` and `output.noncrit` in the same directory.
- `output.crit` is a valid JPEG with all structure preserved, but visually blurry (no AC values).
- `output.noncrit` contains the AC value bits only.

#### Merge a Split JPEG

```
./bin/Release/guetzli --merge output.crit merged.jpg
```
- Requires both `output.crit` and `output.noncrit` in the same directory.
- Produces `merged.jpg`, which is visually and structurally identical to the original `input.jpg`.

#### Standard Compression (Original Guetzli)

```
./bin/Release/guetzli input.jpg output.jpg
```
- Compresses `input.jpg` to `output.jpg` using Guetzli's standard algorithm.

### Notes
- The split/merge process is **lossless** for baseline JPEGs: all quantization tables, component IDs, and SOF markers are preserved.
- Only the AC value bits are separated; all other JPEG data remains in `.crit`.
- Input JPEGs must be baseline (not progressive).
- You can use tools like `djpeg -debug` to verify that `.crit` matches the original JPEG in all structure except for AC values.

## Split/Merge JPEG Functionality

GuetzliSplit can split a JPEG into two files:
- `.crit`: Contains all JPEG structure, metadata, quantization tables, component IDs, DC coefficients, and AC *length* (nbits) values, but the AC *value* bits are zeroed.
- `.noncrit`: Contains only the AC *value* bits for all blocks, in order.

This allows you to store the critical JPEG structure separately from the high-frequency AC data.

## Usage

```
./bin/Release/guetzli [--split] [--merge] [flags] input.jpg output.jpg
```

- Use `--split` to produce `.crit` and `.noncrit` files from a JPEG.
- Use `--merge` to reconstruct a JPEG from `.crit` and `.noncrit` files.
- Without `--split` or `--merge`, GuetzliSplit acts as a standard Guetzli compressor.

## Original Guetzli README

---

## Features
- **Standard Guetzli compression**
- **Split mode:** Output JPEG as two files: `.crit` (all JPEG data except AC coefficients) and `.noncrit` (raw AC coefficient bits)
- **Merge mode:** Reconstruct a JPEG from `.crit` and `.noncrit` files

---

## Build Instructions

1. Open a terminal in the `GuetzliSplit/guetzli-master` directory.
2. On Ubuntu, do `apt-get install libpng-dev`.
3. Run:
   ```sh
   make
   ```
   This will build the `guetzli` binary in `./bin/Release/guetzli`.

---

## Notes
- The `.noncrit` file is always named the same as the `.crit` file, but with `.noncrit` extension.
- The split/merge logic is lossless for the JPEG data.
- Debug output may be printed to the terminal if enabled in the code.

---

## License
This project is based on [Guetzli by Google](https://github.com/google/guetzli) and is licensed under the Apache License, Version 2.0.
