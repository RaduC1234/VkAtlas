cmake_minimum_required(VERSION 3.22.1)

project("atlas_engine")

file(GLOB_RECURSE ENGINE_SOURCES "src/*.cpp" "src/*.hpp")

add_library(atlas_engine SHARED ${ENGINE_SOURCES})

add_subdirectory(dependencies/fmt)
add_subdirectory(dependencies/glm)
add_subdirectory(dependencies/entt)
add_subdirectory(dependencies/VulkanMemoryAllocator)
add_subdirectory(dependencies/tinyobjloader)

add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/stb)

if (ANDROID)
    target_compile_definitions(atlas_engine PUBLIC AVALON_PLATFORM_ANDROID)

    # Find and link GameActivity
    find_package(game-activity REQUIRED CONFIG)

    # Vulkan support via Android NDK
    # Vulkan headers come with the NDK, and loader is on device
    find_library(VULKAN_LIB vulkan REQUIRED)

    target_link_libraries(atlas_engine PUBLIC
            game-activity::game-activity
            ${VULKAN_LIB}
            android
            log
            glm::glm-header-only
            stb_image
            fmt::fmt-header-only
            EnTT::EnTT
            GPUOpen::VulkanMemoryAllocator
            tinyobjloader
    )
elseif (WIN32)

    find_package(Vulkan REQUIRED)
    add_subdirectory(dependencies/glfw)

    add_library(imgui STATIC
            ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/imgui.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/imgui_draw.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/imgui_widgets.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/imgui_demo.cpp   # Optional
            ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/imgui_tables.cpp # Add this line
            ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/backends/imgui_impl_glfw.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/backends/imgui_impl_vulkan.cpp
    )

    target_link_libraries(imgui PRIVATE glfw Vulkan::Vulkan)

    target_include_directories(imgui PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui
            ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/backends
    )

    target_link_libraries(atlas_engine PUBLIC
            glfw
            Vulkan::Vulkan
            glm::glm-header-only
            stb_image
            fmt::fmt-header-only
            EnTT::EnTT
            GPUOpen::VulkanMemoryAllocator
            imgui
            tinyobjloader
    )

    target_compile_definitions(atlas_engine PUBLIC AVALON_PLATFORM_WINDOWS)
    add_custom_command(TARGET atlas_engine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/engine/atlas_engine.dll"
            "${CMAKE_BINARY_DIR}/windows"
            COMMENT "Copying atlas_engine.dll to windows folder"
    )

    add_custom_target(compile_shaders ALL
            COMMAND ${CMAKE_COMMAND} -E echo "Running shader batch script"
            COMMAND "${CMAKE_SOURCE_DIR}/scripts/compile_shaders.bat"
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )

    add_dependencies(atlas_engine compile_shaders)

endif ()

target_include_directories(atlas_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/atlas)
target_include_directories(atlas_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/platform)



