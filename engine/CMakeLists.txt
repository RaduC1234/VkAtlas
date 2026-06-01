cmake_minimum_required(VERSION 3.22)

project("atlas_engine")

option(ATLAS_ENABLE_TRACY_DEBUG "Enable Tracy profiling for Debug builds" ON)

file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/*.hpp")

if (ATLAS_PLATFORM_DESKTOP_BUILD)
        add_library(atlas_engine SHARED ${ENGINE_SOURCES})
else ()
        add_library(atlas_engine STATIC ${ENGINE_SOURCES})
endif ()

atlas_set_output_dirs(atlas_engine)

set_target_properties(atlas_engine PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        WINDOWS_EXPORT_ALL_SYMBOLS ON
)

target_compile_definitions(atlas_engine
        PRIVATE VMA_VULKAN_VERSION=1001000 # VMA use Vulkan 1.1
)

if (ATLAS_PLATFORM_DESKTOP_BUILD)
        target_compile_definitions(atlas_engine
                PRIVATE AVALON_BUILD_SHARED
                INTERFACE AVALON_USE_SHARED
        )
endif ()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)

set(KTX_FEATURE_TESTS      OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_TOOLS      OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_GL_UPLOAD  OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_VK_UPLOAD  OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_LOADTEST_APPS OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)

add_subdirectory(dependencies/KTX-Software)
add_subdirectory(dependencies/fmt)
add_subdirectory(dependencies/glm)
add_subdirectory(dependencies/entt)
add_subdirectory(dependencies/VulkanMemoryAllocator)
add_subdirectory(dependencies/tinyobjloader)
add_subdirectory(dependencies/tinygltf)
add_subdirectory(dependencies/json)

add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/stb)

target_link_libraries(atlas_engine PUBLIC
        glm::glm-header-only
        stb_image
        fmt::fmt-header-only
        EnTT::EnTT
        nlohmann_json::nlohmann_json
)

target_link_libraries(atlas_engine PRIVATE
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

    if (ATLAS_ENABLE_TRACY_DEBUG)
        set(TRACY_ENABLE OFF CACHE BOOL "" FORCE)
        set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)

        add_subdirectory(dependencies/tracy)

        target_link_libraries(atlas_engine PUBLIC Tracy::TracyClient)

        target_compile_definitions(TracyClient PUBLIC
                "$<$<CONFIG:Debug>:TRACY_ENABLE>"
                "$<$<CONFIG:Debug>:TRACY_ON_DEMAND>"
        )

        target_compile_definitions(atlas_engine PUBLIC
                "$<$<CONFIG:Debug>:ATLAS_PROFILE_CPU>"
        )
    endif ()

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

    target_link_libraries(imgui PRIVATE
            glfw
            Vulkan::Vulkan
    )

    target_include_directories(imgui PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui
            ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/backends
    )

    target_sources(atlas_engine PRIVATE $<TARGET_OBJECTS:imgui>)

    target_link_libraries(atlas_engine
        PUBLIC
            Vulkan::Vulkan
        PRIVATE
            glfw
    )

endif ()

target_include_directories(atlas_engine PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/atlas
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
        ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui/backends
        ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/tinygltf
        ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/tinyobjloader
        ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/VulkanMemoryAllocator/include
        ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/KTX-Software/lib/include
)
