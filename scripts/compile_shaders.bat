@echo off
setlocal enabledelayedexpansion

:: Directory where shaders are stored
set SHADER_DIR=assets/shaders

where glslc >nul 2>nul
if %errorlevel% neq 0 (
    echo Error: glslc compiler not found! Ensure Vulkan SDK is installed and glslc is in PATH.
    exit /b 1
)

:: Compile all .vert and .frag shaders in the SHADER_DIR
for %%F in (%SHADER_DIR%\*.vert) do (
    echo Compiling %%F...
    glslc "%%F" -o "%%F.spv"
)

for %%F in (%SHADER_DIR%\*.frag) do (
    echo Compiling %%F...
    glslc "%%F" -o "%%F.spv"
)

echo Compilation completed!
exit /b 0
