CXX = g++
CXXFLAGS = -std=c++17 -I. -D_FILE_OFFSET_BITS=64 `pkg-config fuse3 --cflags`
LDFLAGS = -pthread `pkg-config fuse3 --libs`

# Common source files
COMMON_SRCS = \
    FileHandlers/TextFile.cpp \
    FileHandlers/AbstractFile.cpp \
    FileHandlers/RawFile.cpp \
    FileHandlers/DngFile.cpp \
    FileHandlers/PngFile.cpp \
    FileHandlers/BmpFile.cpp \
    FileHandlers/JpegFile.cpp \
    Utilities/Range.cpp

# Target-specific sources
HANDLER_SRCS = main.cpp $(COMMON_SRCS)
FUSE_SRCS = FUSE/CriticalFUSE.cpp $(COMMON_SRCS)

# Object files
HANDLER_OBJS = $(HANDLER_SRCS:.cpp=.o)
FUSE_OBJS = $(FUSE_SRCS:.cpp=.o)

# Executables
TARGET = HandlerTest
FUSE_TARGET = CriticalFUSE
BITFLIPPER_TARGET = BitFlipper

# Default target
all: $(TARGET) $(FUSE_TARGET) $(BITFLIPPER_TARGET)

# Linking
$(TARGET): $(HANDLER_OBJS)
	$(CXX) $(HANDLER_OBJS) -o $@ $(LDFLAGS)

$(FUSE_TARGET): $(FUSE_OBJS)
	$(CXX) $(FUSE_OBJS) -o $@ $(LDFLAGS)

$(BITFLIPPER_TARGET): BitFlipper.c
	$(CC) -o $@ $<

# Compilation rule
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean
clean:
	rm -f $(HANDLER_OBJS) $(FUSE_OBJS) $(TARGET) $(FUSE_TARGET) $(BITFLIPPER_TARGET)

# Run
run: $(TARGET)
	./$(TARGET)

run_fuse: $(FUSE_TARGET)
	./$(FUSE_TARGET) -f mnt

.PHONY: all clean run run_fuse
