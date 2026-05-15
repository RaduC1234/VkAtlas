#pragma once
#include <string>
#include <vector>

#include "entt/entity/entity.hpp"
#include "entt/entity/registry.hpp"

namespace Atlas {
    class ILoader {
    public:
        virtual ~ILoader() = default;
        virtual std::vector<std::string> extensions() const = 0;

        // parentEntity = entt::null means attach to scene root
        virtual std::vector<entt::entity> importAsset(const std::string &path, entt::registry &registry, entt::entity parentEntity = entt::null) = 0;
        virtual std::vector<std::byte> exportAsset(const std::vector<entt::entity> &entities, const entt::registry &registry) = 0;
    };
}
