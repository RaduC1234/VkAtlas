#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Atlas::Pack {
    struct PackOptions {
        std::filesystem::path outputPath;
        std::filesystem::path rootPath = std::filesystem::current_path();
        std::string mountPoint = "/Game";
        std::vector<std::filesystem::path> inputPaths;
        bool recursive = true;
    };

    struct LoadedPack {
        std::filesystem::path path;
        nlohmann::json index;
    };

    void writePack(const PackOptions &options);
    LoadedPack readPackIndex(const std::filesystem::path &packPath);
    void printPackInfo(const std::filesystem::path &packPath);
    void listPackAssets(const std::filesystem::path &packPath, const std::string &typeFilter = {});
    void extractPackAsset(const std::filesystem::path &packPath, const std::string &assetQuery, const std::filesystem::path &outputPath);
}
