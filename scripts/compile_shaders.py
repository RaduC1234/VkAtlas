import os
import subprocess
import shutil
import sys
from pathlib import Path

# Directory where shaders are stored
SHADER_DIR = Path("assets/shaders")

def check_glslc():
    """Check if glslc is available in PATH."""
    if shutil.which("glslc") is None:
        print("Error: glslc compiler not found! Ensure Vulkan SDK is installed and glslc is in PATH.")
        sys.exit(1)

def remove_spv_files():
    """Remove existing .spv files."""
    spv_files = list(SHADER_DIR.glob("*.spv"))
    if spv_files:
        print(f"Removing existing .spv files from {SHADER_DIR}...")
        for file in spv_files:
            try:
                file.unlink()
            except Exception:
                pass

def compile_shaders(extension):
    """Compile shaders of a given extension."""
    for shader_file in SHADER_DIR.glob(f"*.{extension}"):
        output_file = shader_file.with_suffix(shader_file.suffix + ".spv")
        print(f"Compiling {shader_file}...")
        subprocess.run(["glslc", str(shader_file), "-o", str(output_file)], check=True)

def main():
    check_glslc()
    remove_spv_files()

    # Compile shader types
    for ext in ["vert", "frag", "comp"]:
        compile_shaders(ext)

    print("Compilation completed!")

if __name__ == "__main__":
    main()