#!/bin/bash

# This script builds the project using CMake

set -e  # Exit on any error

echo "🎵 Harmonic Music Streaming Platform - Build Script"
echo "=========================================="

# Check if we're in the correct directory
if [ ! -f "CMakeLists.txt" ]; then
    echo "Error: CMakeLists.txt not found. Please run this script from the project root directory."
    exit 1
fi

# Check and install dependencies on macOS
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "Checking macOS dependencies..."
    
    if ! command -v brew >/dev/null 2>&1; then
        echo "Error: Homebrew not found. Install from https://brew.sh"
        exit 1
    fi
    
    # Check and install required packages
    declare -a packages=("boost" "libshout" "libogg" "libvorbis" "cmake")
    
    for package in "${packages[@]}"; do
        if ! brew list "$package" &>/dev/null; then
            echo "Installing $package..."
            brew install "$package"
        else
            echo "✓ $package already installed"
        fi
    done
fi

# Create build directory if it doesn't exist
if [ ! -d "build" ]; then
    echo "Creating build directory..."
    mkdir build
fi

# Clean previous build
echo "Cleaning previous build..."
cd build
rm -rf *

# Configure with CMake
echo "Configuring with CMake..."
cmake ..

# Build the project
echo "Building project..."
if command -v nproc >/dev/null 2>&1; then
    # Use nproc if available (Linux)
    make -j$(nproc)
else
    # Fallback to 4 jobs (macOS/Windows)
    make -j4
fi

echo ""
echo "✅ Build completed successfully!"
echo "Executable: build/MusicStreamPlatform"
echo ""
echo "To run the application:"
echo "  ./build/MusicStreamPlatform config/dj.config    # DJ mode"
echo "  ./build/MusicStreamPlatform config/coder.config # Coder mode"
echo "  ./build/MusicStreamPlatform config/radio.config # Radio mode"
