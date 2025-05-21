CXX = g++
CXXFLAGS = -std=c++17 -I. -D_FILE_OFFSET_BITS=64 `pkg-config fuse3 --cflags`
LDFLAGS = -pthread `pkg-config fuse3 --libs`

# Source files
SRCS = main.cpp \
       FileHandlers/TextFile.cpp \
       FileHandlers/AbstractFile.cpp \
       Utilities/Range.cpp \
       FUSE/CriticalFUSE.cpp

# Object files
OBJS = $(SRCS:.cpp=.o)

# Executable names
TARGET = HandlerTest
FUSE_TARGET = CriticalFUSE

# Default target
all: $(TARGET) $(FUSE_TARGET)

# Linking
$(TARGET): $(filter-out FUSE/CriticalFUSE.o,$(OBJS))
	$(CXX) $(filter-out FUSE/CriticalFUSE.o,$(OBJS)) -o $(TARGET) $(LDFLAGS)

$(FUSE_TARGET): FUSE/CriticalFUSE.o $(filter-out FUSE/CriticalFUSE.o,$(OBJS))
	$(CXX) FUSE/CriticalFUSE.o $(filter-out FUSE/CriticalFUSE.o,$(OBJS)) -o $(FUSE_TARGET) $(LDFLAGS)

# Compilation
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean
clean:
	rm -f $(OBJS) $(TARGET) $(FUSE_TARGET)

# Run
run: $(TARGET)
	./$(TARGET)

run_fuse: $(FUSE_TARGET)
	./$(FUSE_TARGET) -f mnt

.PHONY: all clean run run_fuse 