#pragma once

#include <entt/entt.hpp>
#include <memory>

#include "renderer/Camera.hpp"
#include "renderer/Renderer.hpp"
#include "system/CameraSystem.hpp"
#include "system/RenderSystem.hpp"

namespace Atlas {
    class Scene {
    public:
        explicit Scene(Renderer &renderer);
        virtual ~Scene() = default;

        virtual void onLoad(entt::registry&& registry);
        virtual void onUpdate(float deltaTime);
        virtual void onRender(float deltaTime, float aspectRatio);
        virtual void onDelete();

        static Scene loadSceneFromJson(const std::string &json);
        static std::string saveSceneToJson(const Scene &scene);
    protected:
        entt::registry registry;
        Renderer& renderer;
        Camera camera{};

        std::unique_ptr<CameraSystem> cameraSystem;
        std::unique_ptr<RenderSystem> renderSystem;
    };
}
