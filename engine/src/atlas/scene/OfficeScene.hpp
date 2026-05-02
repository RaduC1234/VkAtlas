#pragma once

#include <memory>

#include "IScene.hpp"
#include "renderer/Renderer.hpp"
#include "system/CameraSystem.hpp"
#include "system/RenderSystemV2.hpp"

namespace Atlas {

    class OfficeScene : public IScene {
    public:
        explicit OfficeScene(Renderer &renderer);
        ~OfficeScene() override = default;

        void onLoad(entt::registry&& registry) override;
        void onUpdate(float deltaTime) override;
        void onRender(FrameContext frameContext) override;
        void onDelete() override;

    protected:
        std::unique_ptr<CameraSystem> cameraSystem;
        std::unique_ptr<RenderSystemV2> renderSystem;
    };
}
