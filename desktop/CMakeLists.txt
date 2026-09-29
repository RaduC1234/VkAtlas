cmake_minimum_required(VERSION 3.22)

project("atlas_desktop")

add_library(atlas_desktop_runtime_module STATIC
        src/core/DesktopRuntimeModule.cpp
        src/core/DesktopRuntimeDebugLayer.cpp
)
atlas_set_output_dirs(atlas_desktop_runtime_module)

target_link_libraries(atlas_desktop_runtime_module PUBLIC
        atlas_engine
)

add_executable(atlas_desktop
        src/DesktopRuntimeEntryPoint.cpp
)
atlas_set_output_dirs(atlas_desktop)

target_link_libraries(atlas_desktop PRIVATE
        atlas_desktop_runtime_module
)

target_include_directories(atlas_desktop_runtime_module PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if (TARGET atlas_office_project)
    add_dependencies(atlas_desktop atlas_office_project)
endif ()

atlas_copy_runtime_dependencies(atlas_desktop "$<TARGET_FILE_DIR:atlas_desktop>")
