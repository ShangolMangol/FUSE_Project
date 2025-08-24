# Makefile for Fast SSIM C++ Implementation

CXX = g++
CXXFLAGS = -std=c++11 -O3 -march=native -ffast-math
OPENCV_LIBS = -lopencv_core -lopencv_imgproc -lopencv_imgcodecs -lopencv_highgui

# Default target
all: fast_ssim

# Compile the fast SSIM executable
fast_ssim: fast_ssim.cpp
	$(CXX) $(CXXFLAGS) -o fast_ssim fast_ssim.cpp $(OPENCV_LIBS)

# Install dependencies (Ubuntu/Debian)
install-deps:
	sudo apt-get update
	sudo apt-get install -y libopencv-dev build-essential

# Install dependencies (macOS with Homebrew)
install-deps-mac:
	brew install opencv

# Clean build files
clean:
	rm -f fast_ssim

# Test the SSIM implementation
test: fast_ssim
	@echo "Testing SSIM calculation..."
	@echo "Note: You need two test images to run this test"
	@echo "Usage: ./fast_ssim image1.jpg image2.jpg 0"

# Show help
help:
	@echo "Available targets:"
	@echo "  all              - Build fast_ssim executable"
	@echo "  fast_ssim        - Build the SSIM calculator"
	@echo "  install-deps     - Install OpenCV dependencies (Ubuntu/Debian)"
	@echo "  install-deps-mac - Install OpenCV dependencies (macOS)"
	@echo "  clean            - Remove build files"
	@echo "  test             - Test the SSIM implementation"
	@echo "  help             - Show this help message"
	@echo ""
	@echo "Usage:"
	@echo "  ./fast_ssim <image1> <image2> <windowed>"
	@echo "    windowed: 0 for simple SSIM, 1 for windowed SSIM"

.PHONY: all clean test help install-deps install-deps-mac
