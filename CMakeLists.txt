cmake_minimum_required(VERSION 3.10)
project(Atlas)

# Enable C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(WIN32)
    add_subdirectory(windows)
endif ()

add_subdirectory(engine)

