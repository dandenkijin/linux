#!/bin/bash

# Navigate to the project directory
cd "$(dirname "$0")"

# Clean previous build
echo "Cleaning previous build..."
rm -rf target

# Install dependencies if needed
if [ ! -d "node_modules" ]; then
    echo "Installing Node.js dependencies..."
    npm install
fi

# Build the frontend
echo "Building frontend..."
npm run build

# Run the Tauri application
echo "Starting Tauri application..."
RUST_LOG=info,wconf=debug,tao=warn,winit=warn cargo tauri dev -- --no-default-features
