cmake_minimum_required(VERSION 3.22)

project("AtlasOfficeProject")

add_library(atlas_office_project SHARED
        src/core/OfficeProject.cpp
)

target_link_libraries(atlas_office_project PRIVATE atlas_engine)

target_include_directories(atlas_office_project PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

atlas_set_output_dirs(atlas_office_project)
atlas_copy_runtime_dependencies(atlas_office_project "$<TARGET_FILE_DIR:atlas_office_project>")
