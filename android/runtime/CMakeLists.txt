cmake_minimum_required(VERSION 3.22)
project(atlas_runtime)

find_package(game-activity REQUIRED CONFIG)

add_library(atlas_runtime SHARED
        src/AndroidRuntimeEntryPoint.cpp
        src/core/AndroidRuntimeModule.cpp
)

target_link_libraries(atlas_runtime PRIVATE
        atlas_engine
        game-activity::game-activity
        android
        log
)

target_include_directories(atlas_runtime PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
