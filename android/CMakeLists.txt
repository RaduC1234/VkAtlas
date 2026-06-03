cmake_minimum_required(VERSION 3.22)
project(AtlasAndroid)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_compile_definitions(ATLAS_PLATFORM_ANDROID)

option(ATLAS_ENABLE_TRACY_DEBUG "Enable Tracy CPU and GPU profiling in Debug builds" ON)

set(ATLAS_BIN_DIR "${CMAKE_BINARY_DIR}/bin")
set(ATLAS_LIB_DIR "${CMAKE_BINARY_DIR}/lib")

function(atlas_set_output_dirs target)
    set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${ATLAS_BIN_DIR}"
            LIBRARY_OUTPUT_DIRECTORY "${ATLAS_BIN_DIR}"
            ARCHIVE_OUTPUT_DIRECTORY "${ATLAS_LIB_DIR}"
    )
    foreach (OUTPUT_CONFIG DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(${target} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY_${OUTPUT_CONFIG} "${ATLAS_BIN_DIR}"
                LIBRARY_OUTPUT_DIRECTORY_${OUTPUT_CONFIG} "${ATLAS_BIN_DIR}"
                ARCHIVE_OUTPUT_DIRECTORY_${OUTPUT_CONFIG} "${ATLAS_LIB_DIR}"
        )
    endforeach ()
endfunction()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../engine ${CMAKE_CURRENT_BINARY_DIR}/engine)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/runtime  ${CMAKE_CURRENT_BINARY_DIR}/runtime)
