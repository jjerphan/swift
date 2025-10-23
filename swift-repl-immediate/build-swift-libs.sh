#!/bin/bash

# Build script to create Swift compiler libraries needed for SwiftJITREPL

set -e

echo "Building Swift compiler libraries..."

# Change to Swift source directory
cd /home/jjerphan/dev/swift

# Build Swift compiler libraries using incremental preset
# This will build the necessary libraries: swiftFrontend, swiftSILGen, swiftImmediate, etc.
echo "Running Swift build script with incremental preset..."
./utils/build-script --preset=buildbot_incremental,tools=RA,stdlib=RA

echo "Swift compiler libraries built successfully!"
echo "You can now build the SwiftJITREPL project with: ./build.sh"
