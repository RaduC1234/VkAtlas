#pragma once

#include <memory>

#include <Atlas.hpp>

namespace Atlas {
    class AssetManager;

    class OfficeScene : public IScene {
    public:
        OfficeScene(Renderer &renderer, AssetManager &assets);
        ~OfficeScene() override = default;

        void onLoad(entt::registry &&registry) override;
        void onUpdate(float deltaTime) override;
        void onRender(FrameContext frameContext) override;
        void onDelete() override;

    protected:
        AssetManager &assets;
        std::unique_ptr<CameraSystem> cameraSystem;
        std::unique_ptr<RenderSystemV2> renderSystem;
    };
}
