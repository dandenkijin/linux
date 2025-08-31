#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Test script for the Tauri-based kernel configuration tool

echo "Testing wconf - Tauri-based kernel configuration tool"

# Check if we're in the kernel source directory
if [ ! -f "Makefile" ] || [ ! -f "Kconfig" ]; then
    echo "Error: This script must be run from the kernel source directory"
    exit 1
fi

echo "Checking for required tools..."
if ! command -v cargo &> /dev/null; then
    echo "Error: cargo is not installed"
    exit 1
fi

if ! command -v node &> /dev/null; then
    echo "Warning: node is not installed (needed for frontend development)"
fi

echo "Building wconf..."
make scripts/kconfig/wconf

if [ ! -f "scripts/kconfig/wconf/target/release/wconf" ]; then
    echo "Error: Failed to build wconf"
    exit 1
fi

echo "wconf build successful!"

echo "Testing help output..."
make help | grep wconfig

echo "Test completed successfully!"
echo "You can now run 'make wconfig' to launch the Tauri-based configuration tool"
