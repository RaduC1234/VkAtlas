cmake_minimum_required(VERSION 3.22)

project("atlas_editor")

file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/*.hpp")
list(REMOVE_ITEM EDITOR_SOURCES "src/EditorEntryPoint.cpp")

set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "Disable LunaSVG examples" FORCE)
add_subdirectory(dependencies/lunasvg)

find_package(Vulkan REQUIRED)

add_library(imgui OBJECT
        ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/imgui.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/imgui_draw.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/imgui_widgets.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/imgui_demo.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/imgui_tables.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/backends/imgui_impl_glfw.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/backends/imgui_impl_vulkan.cpp
)

set_target_properties(imgui PROPERTIES
        POSITION_INDEPENDENT_CODE ON
)

target_link_libraries(imgui PUBLIC
        glfw
        Vulkan::Vulkan
)

target_include_directories(imgui PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/backends
)

add_library(atlas_editor_module STATIC ${EDITOR_SOURCES})
atlas_set_output_dirs(atlas_editor_module)

target_include_directories(atlas_editor_module PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/ImViewGuizmo
        ${CMAKE_SOURCE_DIR}/engine/dependencies/glfw/include
)

target_link_libraries(atlas_editor_module PUBLIC
        atlas_engine
)

target_link_libraries(atlas_editor_module PRIVATE
        imgui
        tinyobjloader
        tinygltf
        lunasvg::lunasvg
)

target_compile_definitions(atlas_editor_module PRIVATE
        ATLAS_DEFAULT_PROJECT_MANIFEST="${CMAKE_SOURCE_DIR}/samples/office/project.atlas.json"
        ATLAS_DEFAULT_PROJECT_MODULE="$<TARGET_FILE:atlas_office_project>"
)

add_executable(atlas_editor
        src/EditorEntryPoint.cpp
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
