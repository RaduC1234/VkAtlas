#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Atlas {
    struct ProjectManifest {
        std::string name = "Untitled Project";
        std::string engineVersion;
        std::string startupScene;
        std::string assetRoot = "assets";
        std::string codeModule;
        std::vector<std::string> scenes;
    };

    ProjectManifest loadProjectManifest(const std::filesystem::path &manifestPath);
}
