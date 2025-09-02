#!/bin/bash

# Navigate to the project directory
cd "$(dirname "$0")"

# Start a simple HTTP server to serve our test page
PORT=3000

# Check if Python is available
if command -v python3 &> /dev/null; then
    echo "Starting Python HTTP server on port $PORT..."
    python3 -m http.server $PORT &
    SERVER_PID=$!
elif command -v python &> /dev/null; then
    echo "Starting Python HTTP server on port $PORT..."
    python -m SimpleHTTPServer $PORT &
    SERVER_PID=$!
else
    echo "Python not found. Please install Python to run the test server."
    exit 1
fi

# Open the test page in the default browser
if command -v xdg-open &> /dev/null; then
    xdg-open "http://localhost:$PORT/minimal-test.html"
elif command -v open &> /dev/null; then
    open "http://localhost:$PORT/minimal-test.html"
else
    echo "Please open http://localhost:$PORT/minimal-test.html in your browser"
fi

# Function to clean up
cleanup() {
    echo "Shutting down server..."
    kill $SERVER_PID 2>/dev/null
    exit 0
}

# Set up trap to clean up on exit
trap cleanup INT TERM

# Keep the script running
wait $SERVER_PID
