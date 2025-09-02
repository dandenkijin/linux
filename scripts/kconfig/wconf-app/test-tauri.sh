#!/bin/bash

# Navigate to the project directory
cd "$(dirname "$0")"

# Clean previous build
rm -rf node_modules target dist

# Install dependencies
echo "Installing dependencies..."
npm install

# Build the project
echo "Building the project..."
npm run build

# Run the Tauri app
echo "Starting Tauri app..."
cargo tauri dev -- --no-default-features

# If the above fails, try with a simpler command
echo "If the above failed, trying alternative command..."
cargo tauri dev -- --no-default-features -- --no-default-features
