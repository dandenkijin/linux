#!/bin/bash
set -e

# Clean previous builds
echo "Cleaning previous builds..."
rm -rf dist/
mkdir -p dist

# Install frontend dependencies
echo "Installing frontend dependencies..."
npm install

# Build frontend
echo "Building frontend..."
npm run build

# Build Rust backend
echo "Building Rust backend..."
cargo build --release

# Copy necessary files
echo "Copying files..."
cp -r public/* dist/
cp index.html dist/

# Make the binary executable
chmod +x target/release/wconf

echo "Build completed successfully!"
echo "Run the application with: ./target/release/wconf"
