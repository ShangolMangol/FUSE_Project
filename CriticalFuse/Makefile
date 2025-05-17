CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -I.
LDFLAGS = 

# Source files
SRCS = main.cpp \
       FileHandlers/TextFile.cpp \
       FileHandlers/AbstractFile.cpp \
       Utilities/Range.cpp

# Object files
OBJS = $(SRCS:.cpp=.o)

# Executable name
TARGET = CriticalFuse

# Default target
all: $(TARGET)

# Linking
$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)

# Compilation
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean
clean:
	rm -f $(OBJS) $(TARGET)

# Run
run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run 