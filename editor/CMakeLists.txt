cmake_minimum_required(VERSION 3.22)

project("atlas_editor")

file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/*.hpp")
list(REMOVE_ITEM EDITOR_SOURCES "src/core/EditorEntryPoint.cpp")

add_library(atlas_editor_module STATIC ${EDITOR_SOURCES})
atlas_set_output_dirs(atlas_editor_module)

target_link_libraries(atlas_editor_module PUBLIC
        atlas_engine
)

target_compile_definitions(atlas_editor_module PRIVATE
        ATLAS_DEFAULT_PROJECT_MANIFEST="${CMAKE_SOURCE_DIR}/samples/office/project.atlas.json"
        ATLAS_DEFAULT_PROJECT_MODULE="$<TARGET_FILE:atlas_office_project>"
)

add_executable(atlas_editor
        src/core/EditorEntryPoint.cpp
)
atlas_set_output_dirs(atlas_editor)

target_link_libraries(atlas_editor PRIVATE
        atlas_editor_module
)

if (TARGET atlas_office_project)
    add_dependencies(atlas_editor atlas_office_project)
endif ()

atlas_copy_runtime_dependencies(atlas_editor "$<TARGET_FILE_DIR:atlas_editor>")

target_compile_definitions(atlas_editor PRIVATE
        ATLAS_DEFAULT_PROJECT_MANIFEST="${CMAKE_SOURCE_DIR}/samples/office/project.atlas.json"
        ATLAS_DEFAULT_PROJECT_MODULE="$<TARGET_FILE:atlas_office_project>"
)
