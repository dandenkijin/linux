#!/bin/bash

# Build the frontend assets for production
# This script is meant to be called from the Makefile

set -e

# Go to the script directory
cd "$(dirname "$0")"

# Create dist directory if it doesn't exist
mkdir -p dist

# Install dependencies if node_modules doesn't exist
if [ ! -d "node_modules" ]; then
    echo "Installing dependencies..."
    npm install
fi

# Build the frontend
# Set environment variables for the build
export NODE_ENV=production
export VITE_TAURI=true

# Run the build
vite build --emptyOutDir

echo "Frontend assets built successfully in dist/"
