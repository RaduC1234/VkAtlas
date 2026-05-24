#pragma once

#include <filesystem>
#include <string>

namespace Atlas::Editor {
    struct ProjectCreateInfo {
        std::filesystem::path manifestPath;
        std::string name;
    };

    struct ProjectCreateResult {
        std::filesystem::path rootPath;
        std::filesystem::path manifestPath;
        std::string name;
        std::string targetName;
    };

    class ProjectCreator {
    public:
        static ProjectCreateResult create(const ProjectCreateInfo &info);
        static std::string defaultNameForPath(const std::filesystem::path &manifestPath);
    };
}
