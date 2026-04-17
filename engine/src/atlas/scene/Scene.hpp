#pragma once

#include <entt/entity/registry.hpp>

#include "renderer/Camera.hpp"
#include "renderer/Renderer.hpp"

namespace Atlas {
    class Scene {
    public:
        explicit Scene(Renderer &renderer);
        virtual ~Scene() = default;

        virtual void onLoad(entt::registry&& registry);
        virtual void onUpdate(float deltaTime);
        virtual void onRender(VkCommandBuffer graphicsCmdBuffer);
        virtual void onDelete();
    protected:
        entt::registry registry;
        Renderer& renderer;
        Camera camera{};
    };
}
