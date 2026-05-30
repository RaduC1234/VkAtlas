#pragma once

#include "core/Layer.hpp"
#include "project/ProjectInstance.hpp"

#include <filesystem>

namespace Atlas {
    class AssetManager;

    class ProjectLayer final : public Layer {
    public:
        ProjectLayer(
            Renderer &renderer,
            AssetManager &assets,
            std::filesystem::path manifestPath,
            std::filesystem::path moduleOverride = {},
            std::filesystem::path startupLevelOverride = {});

        void onAttach() override;
        void onDetach() override;
        void onUpdate(float deltaTime) override;
        void onRender(FrameContext frameContext) override;
        void loadProject(
            std::filesystem::path newManifestPath,
            std::filesystem::path newModuleOverride = {},
            std::filesystem::path newStartupLevelOverride = {});

        Renderer &getRenderer() { return renderer; }
        const Renderer &getRenderer() const { return renderer; }
        AssetManager &assetManager() { return assets; }
        const AssetManager &assetManager() const { return assets; }
        ProjectInstance &project() { return projectInstance; }
        const ProjectInstance &project() const { return projectInstance; }

    private:
        Renderer &renderer;
        AssetManager &assets;
        std::filesystem::path manifestPath;
        std::filesystem::path moduleOverride;
        std::filesystem::path startupLevelOverride;
        ProjectInstance projectInstance;
    };
}
