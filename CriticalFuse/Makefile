CXX = g++
CXXFLAGS = -std=c++17 -I. -I./GuetzliSplit -D_FILE_OFFSET_BITS=64 `pkg-config fuse3 --cflags`
LDFLAGS = -pthread `pkg-config fuse3 --libs` -ljpeg

# Guetzli source files
GUETZLI_SRCS = \
    GuetzliSplit/guetzli_buffer.cpp \
    GuetzliSplit/jpeg_data_writer.cc \
    GuetzliSplit/jpeg_data_reader.cc \
    GuetzliSplit/processor.cc \
    GuetzliSplit/jpeg_data.cc \
    GuetzliSplit/jpeg_data_encoder.cc \
    GuetzliSplit/jpeg_data_decoder.cc \
    GuetzliSplit/jpeg_huffman_decode.cc \
    GuetzliSplit/entropy_encode.cc \
    GuetzliSplit/quantize.cc \
    GuetzliSplit/quality.cc \
    GuetzliSplit/preprocess_downsample.cc \
    GuetzliSplit/output_image.cc \
    GuetzliSplit/fdct.cc \
    GuetzliSplit/idct.cc \
    GuetzliSplit/dct_double.cc \
    GuetzliSplit/gamma_correct.cc \
    GuetzliSplit/score.cc \
    GuetzliSplit/debug_print.cc

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
HANDLER_SRCS = TextFileSplitTest.cpp $(COMMON_SRCS) $(GUETZLI_SRCS)
FUSE_SRCS = FUSE/CriticalFUSE.cpp $(COMMON_SRCS) $(GUETZLI_SRCS)

# Object files
HANDLER_OBJS = $(HANDLER_SRCS:.cpp=.o)
HANDLER_OBJS := $(HANDLER_OBJS:.cc=.o)
FUSE_OBJS = $(FUSE_SRCS:.cpp=.o)
FUSE_OBJS := $(FUSE_OBJS:.cc=.o)

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
	$(CC) -o $@ $< -lm

# Compilation rule for .cpp files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compilation rule for .cc files
%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean
clean:
	rm -f $(HANDLER_OBJS) $(FUSE_OBJS) $(TARGET) $(FUSE_TARGET) $(BITFLIPPER_TARGET)

# Run
run: $(TARGET)
	./$(TARGET)

run_fuse: $(FUSE_TARGET)
	./$(FUSE_TARGET) -f mnt

# Test targets
test: $(FUSE_TARGET)
	@echo "Running CriticalFUSE tests..."
	@chmod +x tests/*.sh
	@if [ -d "mnt" ] && [ -d "storage" ]; then \
		./tests/run_all_tests.sh ./mnt ./storage; \
	else \
		echo "Error: Mount point 'mnt' or backing directory 'storage' not found."; \
		echo "Please mount CriticalFUSE first: make run_fuse (in another terminal)"; \
		echo "Then run: make test"; \
		exit 1; \
	fi

test_basic: $(FUSE_TARGET)
	@chmod +x tests/*.sh
	@./tests/test_basic_operations.sh ./mnt ./storage

test_file_types: $(FUSE_TARGET)
	@chmod +x tests/*.sh
	@./tests/test_file_types.sh ./mnt ./storage

test_stress: $(FUSE_TARGET)
	@chmod +x tests/*.sh
	@./tests/test_stress.sh ./mnt ./storage

.PHONY: all clean run run_fuse test test_basic test_file_types test_stress
