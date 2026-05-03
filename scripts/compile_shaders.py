import subprocess
import shutil
import sys
from pathlib import Path

SHADER_DIR = Path("assets/shaders")

EXTENSIONS = ["vert", "frag", "comp"]
RT_EXTENSIONS = ["rchit", "rgen", "rmiss", "rahit", "rint", "rcall"]

def check_glslc():
    if shutil.which("glslc") is None:
        print("Error: glslc compiler not found! Ensure Vulkan SDK is installed and glslc is in PATH.")
        sys.exit(1)


def remove_spv_files():
    spv_files = list(SHADER_DIR.glob("*.spv"))
    if spv_files:
        print(f"Removing existing .spv files from {SHADER_DIR}...")
        for file in spv_files:
            try:
                file.unlink()
            except Exception:
                pass


def compile_shaders(extension):
    for shader_file in SHADER_DIR.glob(f"*.{extension}"):
        output_file = shader_file.with_suffix(shader_file.suffix + ".spv")
        print(f"Compiling {shader_file}...")
        subprocess.run(["glslc", str(shader_file), "-o", str(output_file)], check=True)


def compile_rt_shaders(extension):
    for shader_file in SHADER_DIR.glob(f"*.{extension}"):
        output_file = shader_file.with_suffix(shader_file.suffix + ".spv")
        print(f"Compiling RT {shader_file}...")
        subprocess.run(["glslc", str(shader_file), "-o", str(output_file), "--target-env=vulkan1.2"], check=True)


def main():
    check_glslc()
    remove_spv_files()

    for ext in EXTENSIONS:
        compile_shaders(ext)

    for ext in RT_EXTENSIONS:
        compile_rt_shaders(ext)

    print("Compilation completed!")


if __name__ == "__main__":
    main()
