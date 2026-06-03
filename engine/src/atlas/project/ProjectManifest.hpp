#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Atlas {
    struct ProjectManifest {
        std::string name = "Untitled Project";
        std::string engineVersion;
        std::string startupLevel;
        std::string assetRoot = "assets";
        std::string codeModule;
        std::vector<std::string> levels;
    };

    ProjectManifest loadProjectManifest(const std::filesystem::path &manifestPath);
}
