#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "entt/entity/entity.hpp"
#include "entt/entity/registry.hpp"
#include "asset/EntityBuffer.hpp"

namespace Atlas {
    class IAccessor {
    public:
        virtual ~IAccessor() = default;
        virtual std::vector<std::string> extensions() const = 0;

        virtual void importAsset(const std::string &path, EntityBuffer &buffer) = 0;
        virtual std::vector<std::byte> exportAsset(const std::vector<entt::entity> &entities, const entt::registry &registry) = 0;
    };
}
