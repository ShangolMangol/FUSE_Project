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
    GuetzliSplit/debug_print.cc \
    GuetzliSplit/butteraugli_comparator.cc

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
TEST_SRCS = test_exact_jpeg_parsing.cpp $(COMMON_SRCS) $(GUETZLI_SRCS)

# Object files
HANDLER_OBJS = $(HANDLER_SRCS:.cpp=.o)
HANDLER_OBJS := $(HANDLER_OBJS:.cc=.o)
FUSE_OBJS = $(FUSE_SRCS:.cpp=.o)
FUSE_OBJS := $(FUSE_OBJS:.cc=.o)
TEST_OBJS = $(TEST_SRCS:.cpp=.o)
TEST_OBJS := $(TEST_OBJS:.cc=.o)

# Executables
TARGET = HandlerTest
FUSE_TARGET = CriticalFUSE
BITFLIPPER_TARGET = BitFlipper
TEST_TARGET = test_exact_jpeg_parsing

# Default target
all: $(TARGET) $(FUSE_TARGET) $(BITFLIPPER_TARGET) $(TEST_TARGET)

# Linking
$(TARGET): $(HANDLER_OBJS)
	$(CXX) $(HANDLER_OBJS) -o $@ $(LDFLAGS)

$(FUSE_TARGET): $(FUSE_OBJS)
	$(CXX) $(FUSE_OBJS) -o $@ $(LDFLAGS)

$(TEST_TARGET): $(TEST_OBJS)
	$(CXX) $(TEST_OBJS) -o $@ $(LDFLAGS)

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
	rm -f $(HANDLER_OBJS) $(FUSE_OBJS) $(TARGET) $(FUSE_TARGET) $(BITFLIPPER_TARGET) $(TEST_OBJS) $(TEST_TARGET)

# Run
run: $(TARGET)
	./$(TARGET)

run_fuse: $(FUSE_TARGET)
	./$(FUSE_TARGET) -f mnt

.PHONY: all clean run run_fuse
