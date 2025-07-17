# FUSE Project

## Compilation
To compile the project inside the folder `CriticalFuse`:
```bash
make
```

This will build:
- CriticalFUSE (the main FUSE filesystem)
- HandlerTest (for testing txt file split)
- BitFlipper (for bit manipulation over non-critical data)

### Makefile Targets
The Makefile provides several targets:

- `make` or `make all`: Builds all executables
- `make clean`: Removes all compiled objects and executables
- `make run`: Runs the HandlerTest executable
- `make run_fuse`: Runs the CriticalFUSE filesystem in foreground mode

To build specific components:
```bash
make CriticalFUSE    # Build only the FUSE filesystem
make HandlerTest     # Build only the handler test
make BitFlipper      # Build only the bit flipper tool
```

## Running the FUSE Filesystem
```bash
./CriticalFUSE -f mnt
```
- `-f`: Run in foreground
- `-d`: Enable debug output

To unmount:
```bash
fusermount3 -u ./mnt
```

## Adding a New File Handler

To add support for a new file type in the Critical Fuse system, follow these steps:

1. **Create a New Handler Class:**
   - In the `CriticalFuse/FileHandlers/` directory, create a new C++ class for your file type (e.g., `MyFileHandler.cpp` and `MyFileHandler.h`).
   - Inherit from the abstract base class `AbstractFileHandler`.

2. **Implement Required Methods:**
   - At minimum, implement the mapping/splitting function(s) specific to your file type, as well as any other pure virtual methods from `AbstractFileHandler`.
   - Example skeleton:
     ```cpp
     // MyFileHandler.h
     #include "AbstractFile.h"
     class MyFileHandler : public AbstractFileHandler {
     public:
         MyFileHandler(const std::string& path);
         ResultCode createMapping(const char* buffer, size_t size) override;
         // Implement other required methods...
     };
     ```

3. **Register the New Handler:**
   - In the main FUSE logic (typically in `CriticalFUSE.cpp`), locate the section where file handlers are selected based on file type, in the function `getFileHandler`.
   - Add an `if` or `switch` statement to instantiate your new handler for the appropriate file extension or magic number.
   - Example:
     ```cpp
     if (extension == ".mytype") {
         fileHandler = std::make_unique<MyFileHandler>(path);
     }
     ```

4. **Update the Build System:**
   - Add your new `.cpp` file to the `CriticalFuse/Makefile` so it is compiled and linked.

5. **Run The System With Your Handler:**
   - Build the project with `make` and test your handler using the FUSE filesystem.

**Summary:**
- Inherit from `AbstractFileHandler`.
- Implement required methods for your file type.
- Register your handler in the main FUSE logic.
- Add your files to the Makefile.
- Test thoroughly.

For more details, refer to the existing handlers in `CriticalFuse/FileHandlers/` (e.g., `JpegFile.cpp`, `TextFile.cpp`).

## BitFlipper Tool
The BitFlipper tool allows you to flip bits in files, either completely or randomly. This is useful for testing file corruption scenarios and error resilience.

### Usage

1. Normal Mode (flip all bits in a range):
```bash
./BitFlipper <file> <start_offset> <end_offset>
```
Example:
```bash
./BitFlipper test.txt 0 1000  # Flips all bits in bytes 0-1000
```

2. Random Mode (flip random bits in entire file):
```bash
./BitFlipper -r <percentage> <file>
```
Example:
```bash
./BitFlipper -r 25 test.txt  # Randomly flips 25% of all bits in the file
```

### Features
- Normal mode: Flips all bits in a specified range
- Random mode: Randomly flips a specified percentage of bits throughout the entire file
- Efficient processing using buffered I/O
- Proper error handling and validation
- Progress reporting

### Notes
- The random mode uses a uniform distribution to ensure even spread of bit flips
- File modifications are done in-place
- Always make a backup of important files before using the BitFlipper

## Project Structure
The project is organized as follows:
- `FUSE/`: Contains the main FUSE filesystem implementation
- `FileHandlers/`: Contains handlers for different file types
- `Utilities/`: Contains utility classes and functions
- `Makefile`: Build configuration for the project

