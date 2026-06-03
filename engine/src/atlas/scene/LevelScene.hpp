#pragma once

#include <filesystem>
#include <memory>

#include "asset/AssetManager.hpp"
#include "core/Core.hpp"
#include "scene/IScene.hpp"
#include "system/CameraSystem.hpp"

namespace Atlas {
    class AVALON_API LevelScene final : public IScene {
    public:
        LevelScene(Renderer &renderer, AssetManager &assets, std::filesystem::path levelPath, std::filesystem::path projectRoot = {}, std::filesystem::path assetRoot = {});

        void onLoad(entt::registry &&loadedRegistry) override;
        void onUpdate(float deltaTime) override;
        void onRender(FrameContext frameContext) override;
        void onDelete() override;

    private:
        AssetManager &assets;
        std::filesystem::path levelPath;
        std::filesystem::path projectRoot;
        std::filesystem::path assetRoot;
        std::unique_ptr<CameraSystem> cameraSystem;
        std::unique_ptr<RenderSystemV2> renderSystem;

        void ensureCamera();
        void ensureSkybox();
        entt::entity activeCamera();
        void updateCameras();
    };
}
