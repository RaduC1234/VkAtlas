cmake_minimum_required(VERSION 3.22)
project(Atlas)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(ATLAS_ENABLE_TRACY_DEBUG "Enable Tracy CPU and GPU profiling in Debug builds" ON)

set(ATLAS_BIN_DIR "${CMAKE_BINARY_DIR}/bin")
set(ATLAS_LIB_DIR "${CMAKE_BINARY_DIR}/lib")

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

if (WIN32 OR (UNIX AND NOT APPLE AND NOT ANDROID))
    set(ATLAS_PLATFORM_DESKTOP_BUILD ON)
    add_compile_definitions(ATLAS_PLATFORM_DESKTOP)
endif ()

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

function(atlas_copy_runtime_dependencies target destination)
    if (WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${destination}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_RUNTIME_DLLS:${target}>
                        "${destination}"
                COMMAND_EXPAND_LISTS
        )
    endif ()
endfunction()

add_subdirectory(engine)

# Desktop umbrella
if (WIN32 OR (UNIX AND NOT APPLE AND NOT ANDROID))
    add_subdirectory(desktop_runtime)
    add_subdirectory(editor)
endif ()

