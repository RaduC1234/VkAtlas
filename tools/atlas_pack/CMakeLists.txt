add_executable(atlas_pack
        src/AtlasPack.cpp
        src/AtlasPack.hpp
        src/main.cpp
)

atlas_set_output_dirs(atlas_pack)

target_link_libraries(atlas_pack PRIVATE
        nlohmann_json::nlohmann_json
)
