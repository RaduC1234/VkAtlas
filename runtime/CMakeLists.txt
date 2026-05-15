cmake_minimum_required(VERSION 3.22)

project("atlas_runtime")

add_library(atlas_runtime_module STATIC
        src/core/RuntimeModule.cpp
)
atlas_set_output_dirs(atlas_runtime_module)

target_link_libraries(atlas_runtime_module PUBLIC
        atlas_engine
)

option(ATLAS_RUNTIME_ENABLE_IMGUI "Enable the runtime ImGui debug overlay" OFF)

if (ATLAS_RUNTIME_ENABLE_IMGUI)
    target_sources(atlas_runtime_module PRIVATE
            src/RuntimeDebugLayer.cpp
            src/RuntimeDebugLayer.hpp
    )
    target_compile_definitions(atlas_runtime_module PRIVATE ATLAS_RUNTIME_ENABLE_IMGUI)
endif ()

target_compile_definitions(atlas_runtime_module PRIVATE
        ATLAS_DEFAULT_PROJECT_MANIFEST="${CMAKE_SOURCE_DIR}/samples/office/project.atlas.json"
        ATLAS_DEFAULT_PROJECT_MODULE="$<TARGET_FILE:atlas_office_project>"
)

add_executable(atlas_runtime
        src/core/RuntimeEntryPoint.cpp
)
atlas_set_output_dirs(atlas_runtime)

target_link_libraries(atlas_runtime PRIVATE
        atlas_runtime_module
)

if (TARGET atlas_office_project)
    add_dependencies(atlas_runtime atlas_office_project)
endif ()

atlas_copy_runtime_dependencies(atlas_runtime "$<TARGET_FILE_DIR:atlas_runtime>")

target_compile_definitions(atlas_runtime PRIVATE
        ATLAS_DEFAULT_PROJECT_MANIFEST="${CMAKE_SOURCE_DIR}/samples/office/project.atlas.json"
        ATLAS_DEFAULT_PROJECT_MODULE="$<TARGET_FILE:atlas_office_project>"
)
