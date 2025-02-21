cmake_minimum_required(VERSION 3.22.1)

project("atlas_engine")

file(GLOB_RECURSE ENGINE_SOURCES "src/*.cpp" "src/*.hpp")

add_library(atlas_engine SHARED ${ENGINE_SOURCES})

if (ANDROID)
    target_compile_definitions(atlas_engine PUBLIC AVALON_PLATFORM_ANDROID)

    find_package(game-activity REQUIRED CONFIG)

    target_link_libraries(atlas_engine PUBLIC
            game-activity::game-activity
            EGL
            GLESv3
            jnigraphics
            android
            log
    )
elseif (WIN32)

    find_package(Vulkan REQUIRED)
    add_subdirectory(dependencies/fmt)
    add_subdirectory(dependencies/glfw)
    add_subdirectory(dependencies/glm)
    add_subdirectory(dependencies/entt)

    add_library(stb_image INTERFACE)
    target_include_directories(stb_image INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/stb)

    target_link_libraries(atlas_engine PUBLIC
            glfw
            Vulkan::Vulkan
            glm::glm-header-only
            stb_image
            fmt::fmt-header-only
            EnTT::EnTT
    )

    target_compile_definitions(atlas_engine PUBLIC AVALON_PLATFORM_WINDOWS)
    add_custom_command(TARGET atlas_engine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/engine/atlas_engine.dll"
            "${CMAKE_BINARY_DIR}/windows"
            COMMENT "Copying atlas_engine.dll to windows folder"
    )
endif ()

target_include_directories(atlas_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/atlas)
