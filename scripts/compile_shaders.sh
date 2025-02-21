#!/bin/bash

SHADER_DIR="shaders"

# Check if glslc is available
if ! command -v glslc &> /dev/null; then
    echo "[ERROR] glslc compiler not found! Ensure Vulkan SDK is installed and glslc is in PATH."
    exit 1
fi

# Compile all .vert and .frag shaders in the SHADER_DIR
for shader in "$SHADER_DIR"/*.vert "$SHADER_DIR"/*.frag; do
    # Skip if no files found
    [ -e "$shader" ] || continue

    echo "Compiling $shader..."
    glslc "$shader" -o "$shader.spv"

    # Check if compilation was successful
    if [ $? -ne 0 ]; then
        echo "[ERROR] Failed to compile $shader"
        exit 1
    fi
done

echo "Compilation completed successfully!"
exit 0
