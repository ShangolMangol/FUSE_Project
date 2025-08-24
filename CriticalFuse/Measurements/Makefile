# Makefile for Fast SSIM C++ Implementation

CXX = g++
CXXFLAGS = -std=c++11 -O3 -march=native -ffast-math

# Try to find OpenCV using pkg-config, with fallbacks
OPENCV_CFLAGS := $(shell pkg-config --cflags opencv4 2>/dev/null || pkg-config --cflags opencv 2>/dev/null || echo "")
OPENCV_LIBS := $(shell pkg-config --libs opencv4 2>/dev/null || pkg-config --libs opencv 2>/dev/null || echo "-lopencv_core -lopencv_imgproc -lopencv_imgcodecs -lopencv_highgui")

# Default target
all: fast_ssim

# Compile the fast SSIM executable
fast_ssim: fast_ssim.cpp
	$(CXX) $(CXXFLAGS) $(OPENCV_CFLAGS) -o fast_ssim fast_ssim.cpp $(OPENCV_LIBS)

# Alternative compilation without pkg-config (fallback)
fast_ssim-fallback: fast_ssim.cpp
	$(CXX) $(CXXFLAGS) -o fast_ssim fast_ssim.cpp -lopencv_core -lopencv_imgproc -lopencv_imgcodecs -lopencv_highgui

# Install dependencies (Ubuntu/Debian)
install-deps:
	sudo apt-get update
	sudo apt-get install -y libopencv-dev build-essential pkg-config

# Install dependencies (macOS with Homebrew)
install-deps-mac:
	brew install opencv

# Check OpenCV installation
check-opencv:
	@echo "Checking OpenCV installation..."
	@echo "pkg-config opencv4:"
	@pkg-config --cflags --libs opencv4 2>/dev/null || echo "opencv4 not found"
	@echo "pkg-config opencv:"
	@pkg-config --cflags --libs opencv 2>/dev/null || echo "opencv not found"
	@echo "OpenCV headers location:"
	@find /usr -name "opencv.hpp" 2>/dev/null | head -5 || echo "opencv.hpp not found in /usr"

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
	@echo "  fast_ssim-fallback - Build using fallback OpenCV flags"
	@echo "  check-opencv     - Check OpenCV installation"
	@echo "  install-deps     - Install OpenCV dependencies (Ubuntu/Debian)"
	@echo "  install-deps-mac - Install OpenCV dependencies (macOS)"
	@echo "  clean            - Remove build files"
	@echo "  test             - Test the SSIM implementation"
	@echo "  help             - Show this help message"
	@echo ""
	@echo "Usage:"
	@echo "  ./fast_ssim <image1> <image2> <windowed>"
	@echo "    windowed: 0 for simple SSIM, 1 for windowed SSIM"

.PHONY: all clean test help install-deps install-deps-mac check-opencv fast_ssim-fallback
