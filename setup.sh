#!/bin/bash
set -e

echo "=== FeliCa Balance Reader Environment Setup ==="

# Check if uv is installed
if ! command -v uv &> /dev/null; then
    echo "Error: 'uv' is not installed. Please install 'uv' first."
    echo "Visit https://docs.astral.sh/uv/getting-started/installation/ for instructions."
    exit 1
fi

# Initialize uv project if pyproject.toml does not exist
if [ ! -f pyproject.toml ]; then
    echo "Initializing python project with 'uv init'..."
    uv init
else
    echo "pyproject.toml already exists, skipping 'uv init'."
fi

# Add ufbt to development dependencies
echo "Adding 'ufbt' to development dependencies..."
uv add --dev ufbt

# Download and update Flipper Zero SDK and compiler toolchain
echo "Downloading Flipper Zero SDK and toolchain..."
uv run ufbt update

# Build the FeliCa Balance app to verify setup
echo "Verifying environment by compiling the app..."
uv run ufbt faps

echo "=============================================="
echo "=== Setup Completed Successfully! ==="
echo "=============================================="
echo "You can now build the app using: 'uv run ufbt faps'"
echo "Or launch it directly on your Flipper Zero using: 'uv run ufbt launch'"
