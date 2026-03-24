cmake_minimum_required(VERSION 3.22)
project(Atlas)

# Enable C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Platform compile definitions (move platform detection to build system)
if (WIN32)
    add_compile_definitions(ATLAS_PLATFORM_WINDOWS)
elseif(ANDROID)
    add_compile_definitions(ATLAS_PLATFORM_ANDROID)
elseif(APPLE)
    add_compile_definitions(ATLAS_PLATFORM_MACOS)
elseif(UNIX)
    add_compile_definitions(ATLAS_PLATFORM_LINUX)
endif()

# Desktop umbrella
if (WIN32 OR (UNIX AND NOT APPLE AND NOT ANDROID))
    add_compile_definitions(ATLAS_PLATFORM_DESKTOP)
    add_subdirectory(desktop)
endif ()

add_subdirectory(engine)
add_subdirectory(helpers/brdf_lut)