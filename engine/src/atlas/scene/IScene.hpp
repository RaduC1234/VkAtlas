#pragma once

#include <entt/entity/registry.hpp>

#include "renderer/Camera.hpp"
#include "renderer/Renderer.hpp"

namespace Atlas {
    class IScene {
    public:
        explicit IScene(Renderer &renderer);
        virtual ~IScene() = default;

        virtual void onLoad(entt::registry &&registry);
        virtual void onUpdate(float deltaTime);
        virtual void onRender(FrameContext frameContext);
        virtual void onDelete() { registry.clear(); }

    protected:
        entt::registry registry;
        Renderer &renderer;
    };
}
