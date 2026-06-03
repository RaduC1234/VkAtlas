#pragma once

#include <string>
#include <vector>

#include <Atlas.hpp>

namespace Atlas::Editor {
    class ProjectResourceImporter {
    public:
        static std::vector<std::string> supportedExtensions();

        static std::vector<entt::entity> importIntoProject(
            ProjectLayer &projectLayer,
            const std::string &sourcePath,
            entt::registry &registry);

        static void persistImportedResources(
            ProjectLayer &projectLayer,
            const std::string &sourcePath,
            const std::vector<entt::entity> &entities,
            entt::registry &registry);
    };
}
