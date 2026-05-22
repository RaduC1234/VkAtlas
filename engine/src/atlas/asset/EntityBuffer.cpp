#include "EntityBuffer.hpp"

namespace Atlas {
    void EntityBuffer::next() {
        entities_.push_back({});
    }

    std::vector<entt::entity> EntityBuffer::flush(entt::registry &registry) {
        std::vector<entt::entity> created;

        for (auto &desc : entities_) {
            if (desc.components.empty()) {
                continue;
            }

            auto entity = registry.create();
            created.push_back(entity);
            for (auto &box : desc.components)
                box->emplace(registry, entity);
        }
        entities_.clear();
        return created;
    }

    EntityBuffer::EntityDesc & EntityBuffer::current() {
        if (entities_.empty()) entities_.push_back({});
        return entities_.back();
    }
}
