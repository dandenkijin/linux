#!/bin/bash

# Get the directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WCONF_APP_DIR="${SCRIPT_DIR}/wconf-app"

# Check if we're in the correct directory
if [ ! -d "${WCONF_APP_DIR}" ]; then
    echo "Error: Could not find wconf-app directory at ${WCONF_APP_DIR}"
    exit 1
fi

# Check for Node.js and npm
if ! command -v node &> /dev/null || ! command -v npm &> /dev/null; then
    echo "Error: Node.js and npm are required for development"
    echo "Please install Node.js from https://nodejs.org/"
    exit 1
fi

# Install dependencies if needed
if [ ! -d "${WCONF_APP_DIR}/node_modules" ]; then
    echo "Installing development dependencies..."
    (cd "${WCONF_APP_DIR}" && npm install) || {
        echo "Failed to install dependencies"
        exit 1
    }
fi

# Build wconf with development mode enabled
echo "Building wconf with development mode enabled..."
make -C "${SCRIPT_DIR}" clean
make -C "${SCRIPT_DIR}" HOSTCFLAGS_wconf.o="-DDEVELOPMENT_MODE=1" wconf || {
    echo "Failed to build wconf with development mode"
    exit 1
}

# Start the development server
echo "Starting development server..."
(cd "${WCONF_APP_DIR}" && npm run dev)
