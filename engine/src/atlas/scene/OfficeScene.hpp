#pragma once

#include <memory>

#include "Scene.hpp"
#include "renderer/Renderer.hpp"
#include "system/CameraSystem.hpp"
#include "system/RenderSystemV2.hpp"

#ifdef _WIN32
#endif

namespace Atlas {


    class OfficeScene : public Scene {
    public:
        explicit OfficeScene(Renderer &renderer);
        ~OfficeScene() override = default;

        void onLoad(entt::registry&& registry) override;
        void onUpdate(float deltaTime) override;
        void onRender(FrameContext frameContext) override;
        void onDelete() override;
    protected:

        // Systems
        std::unique_ptr<CameraSystem> cameraSystem;
        std::unique_ptr<RenderSystemV2> renderSystem;
    };
}
