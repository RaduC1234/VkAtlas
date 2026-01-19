#pragma once

#include <memory>

#include "renderer/Camera.hpp"
#include "renderer/Renderer.hpp"
#include "system/CameraSystem.hpp"
#include "system/RenderSystem.hpp"
#include "system/SkyboxSystem.hpp"

#ifdef _WIN32
#include <imgui.h>
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
