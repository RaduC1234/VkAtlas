cmake_minimum_required(VERSION 3.22)

project("atlas_desktop_runtime")

add_library(atlas_desktop_runtime_module STATIC
        src/core/DesktopRuntimeModule.cpp
)
atlas_set_output_dirs(atlas_desktop_runtime_module)

target_link_libraries(atlas_desktop_runtime_module PUBLIC
        atlas_engine
)

add_executable(atlas_desktop_runtime
        src/core/DesktopRuntimeEntryPoint.cpp
)
atlas_set_output_dirs(atlas_desktop_runtime)

target_link_libraries(atlas_desktop_runtime PRIVATE
        atlas_desktop_runtime_module
)

if (TARGET atlas_office_project)
    add_dependencies(atlas_desktop_runtime atlas_office_project)
endif ()

atlas_copy_runtime_dependencies(atlas_desktop_runtime "$<TARGET_FILE_DIR:atlas_desktop_runtime>")
