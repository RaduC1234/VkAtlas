#pragma once

#include <Atlas.hpp>
#include <future>

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
        std::future<entt::registry> importFuture;
        bool importComplete{false};

        bool rayTracingReady() const;
        ViewMode activeViewMode() const;
    };
}
