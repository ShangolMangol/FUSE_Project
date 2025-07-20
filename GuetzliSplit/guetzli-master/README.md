<p align="center"><img src="https://cloud.githubusercontent.com/assets/203457/24553916/1f3f88b6-162c-11e7-990a-731b2560f15c.png" alt="Guetzli" width="64"></p>

# GuetzliSplit

GuetzliSplit is a modified version of the Guetzli JPEG encoder that supports splitting JPEG files into critical and non-critical parts, and merging them back into a standard JPEG.

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

## Usage

### 1. **Standard Guetzli Compression**
Compress a JPEG as usual:
```sh
./bin/Release/guetzli input.jpg output.jpg
```

### 2. **Split a JPEG into .crit and .noncrit**
Split a JPEG into critical and non-critical files:
```sh
./bin/Release/guetzli --split-jpeg input.jpg output.crit
```
- This will create:
  - `output.crit` (all JPEG data except AC bits)
  - `output.noncrit` (raw AC coefficient bits)

### 3. **Merge .crit and .noncrit Back into a JPEG**
Reconstruct a JPEG from split files:
```sh
./bin/Release/guetzli --merge-jpeg output.crit merged.jpg
```
- This will read:
  - `output.crit`
  - `output.noncrit` (must be in the same directory)
- It will produce:
  - `merged.jpg` (the reconstructed JPEG)

---

## Notes
- The `.noncrit` file is always named the same as the `.crit` file, but with `.noncrit` extension.
- The split/merge logic is lossless for the JPEG data.
- Debug output may be printed to the terminal if enabled in the code.

---

## License
This project is based on [Guetzli by Google](https://github.com/google/guetzli) and is licensed under the Apache License, Version 2.0.
