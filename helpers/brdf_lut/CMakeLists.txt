cmake_minimum_required(VERSION 3.20)

project(brdf_lut LANGUAGES CXX)

add_executable(brdf_lut
        src/main.cpp
)

target_compile_features(brdf_lut PRIVATE cxx_std_20)

# Reuse the repo's vendored GLM targets if they were already added (engine adds them).
# Only add GLM as a fallback when building this helper standalone.
if (NOT TARGET glm::glm-header-only)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../engine/dependencies/glm" "${CMAKE_CURRENT_BINARY_DIR}/glm")
endif ()

target_link_libraries(brdf_lut PRIVATE glm::glm-header-only)
