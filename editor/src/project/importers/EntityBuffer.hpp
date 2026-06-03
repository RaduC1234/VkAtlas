#pragma once

#include <memory>
#include <utility>
#include <vector>
#include <entt/entt.hpp>

namespace Atlas::Editor {

    class EntityBuffer {
    public:
        template<typename T>
        void add(T component);
        void next();
        std::vector<entt::entity> flush(entt::registry &registry);

        bool empty() const { return entities_.empty(); }

    private:
        struct IComponentBox {
            virtual ~IComponentBox() = default;
            virtual void emplace(entt::registry &registry, entt::entity entity) = 0;
        };

        template<typename T>
        struct ComponentBox final : IComponentBox {
            T component;

            explicit ComponentBox(T c) : component(std::move(c)) {}

            void emplace(entt::registry &registry, entt::entity entity) override;
        };

        struct EntityDesc {
            std::vector<std::unique_ptr<IComponentBox>> components;
        };

        EntityDesc &current();

        std::vector<EntityDesc> entities_;
    };

    template<typename T>
    void EntityBuffer::add(T component) {
        current().components.push_back(std::make_unique<ComponentBox<T>>(std::move(component)));
    }

    template<typename T>
    void EntityBuffer::ComponentBox<T>::emplace(entt::registry &registry, entt::entity entity) {
        registry.emplace<T>(entity, std::move(component));
    }
}
