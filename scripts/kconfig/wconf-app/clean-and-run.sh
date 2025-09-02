#!/bin/bash

# Navigate to the project directory
cd "$(dirname "$0")"

# Clean up previous builds
echo "Cleaning up previous builds..."
rm -rf node_modules target dist

# Install dependencies
echo "Installing dependencies..."
npm install

# Build the frontend
echo "Building frontend..."
npm run build

# Run the Tauri application
echo "Starting Tauri application..."
RUST_LOG=info,wconf=debug,tao=warn,winit=warn cargo tauri dev -- --no-default-features
