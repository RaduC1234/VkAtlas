#pragma once

#include "renderer/Camera.hpp"
#include "renderer/Renderer.hpp"
#include "system/RenderSystem.hpp"

#ifdef ATLAS_PLATFORM_DESKTOP
#endif

namespace Atlas {
    class Scene {
    public:
        explicit Scene(Renderer &renderer);
        virtual ~Scene() = default;

        virtual void onLoad(entt::registry&& registry);
        virtual void onUpdate(float deltaTime);
        virtual void onRender(float deltaTime, float aspectRatio);
        virtual void onDelete();
    protected:
        entt::registry registry;
        Renderer& renderer;
        Camera camera{};
    };
}
