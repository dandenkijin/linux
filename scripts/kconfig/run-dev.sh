#!/bin/bash

# Navigate to the wconf-app directory
cd "$(dirname "$0")/wconf-app"

# Check if the development dependencies are installed
if [ ! -d "node_modules" ]; then
    echo "Installing development dependencies..."
    npm install || { echo "Failed to install dependencies"; exit 1; }
fi

# Build the development version of wconf
make -C "$(dirname "$0")" clean
make -C "$(dirname "$0")" HOSTCFLAGS_wconf.o="-DDEVELOPMENT_MODE=1" wconf

# Start the development server
echo "Starting development server..."
npm run dev
