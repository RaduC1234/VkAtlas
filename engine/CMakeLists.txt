cmake_minimum_required(VERSION 3.22)

project("atlas_engine")

file(GLOB_RECURSE ENGINE_SOURCES "src/*.cpp" "src/*.hpp")

add_library(atlas_engine SHARED ${ENGINE_SOURCES})

target_compile_definitions(atlas_engine
        PRIVATE AVALON_BUILD_SHARED
        INTERFACE AVALON_USE_SHARED
)

if (NOT WIN32)
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set_target_properties(atlas_engine PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
        target_compile_options(atlas_engine PRIVATE -fvisibility=hidden)
    endif ()
endif ()

set(KTX_FEATURE_TESTS      OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_TOOLS      OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_GL_UPLOAD  OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_VK_UPLOAD  OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_LOADTEST_APPS OFF CACHE BOOL "" FORCE)

add_subdirectory(dependencies/KTX-Software)
add_subdirectory(dependencies/fmt)
add_subdirectory(dependencies/glm)
add_subdirectory(dependencies/entt)
add_subdirectory(dependencies/VulkanMemoryAllocator)
add_subdirectory(dependencies/tinyobjloader)
add_subdirectory(dependencies/tinygltf)

add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/stb)

# Link common libraries across all platforms
target_link_libraries(atlas_engine PUBLIC
        glm::glm-header-only
        stb_image
        fmt::fmt-header-only
        EnTT::EnTT
        GPUOpen::VulkanMemoryAllocator
        tinyobjloader
        tinygltf
        ktx
)

# Platform-specific configurations
if (ANDROID)
    find_package(game-activity REQUIRED CONFIG)
    find_library(VULKAN_LIB vulkan REQUIRED)

    target_link_libraries(atlas_engine PUBLIC
            game-activity::game-activity
            ${VULKAN_LIB}
            android
            log
    )

elseif (WIN32)
    find_package(Vulkan REQUIRED)
    add_subdirectory(dependencies/glfw)

    add_library(imgui STATIC
            ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/imgui.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/imgui_draw.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/imgui_widgets.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/imgui_demo.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/imgui_tables.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/backends/imgui_impl_glfw.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/backends/imgui_impl_vulkan.cpp
    )

    target_link_libraries(imgui PRIVATE
            glfw
            Vulkan::Vulkan
    )

    target_include_directories(imgui PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui
            ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/backends
    )

    target_link_libraries(atlas_engine PUBLIC
            glfw
            Vulkan::Vulkan
            imgui
    )

    add_custom_command(TARGET atlas_engine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/engine/atlas_engine.dll"
            "${CMAKE_BINARY_DIR}/desktop"
            COMMENT "Copying atlas_engine.dll to desktop folder"
    )

endif ()

target_include_directories(atlas_engine PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/atlas
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
)
