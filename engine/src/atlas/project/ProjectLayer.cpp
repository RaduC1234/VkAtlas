#include "project/ProjectLayer.hpp"

#include <utility>

#include "core/Profiler.hpp"
#include "renderer/Renderer.hpp"

namespace Atlas {
    ProjectLayer::ProjectLayer(Renderer &renderer, AssetManager &assets, std::filesystem::path manifestPath, std::filesystem::path moduleOverride, std::filesystem::path startupLevelOverride)
        : Layer("ProjectLayer"),
          renderer(renderer),
          assets(assets),
          manifestPath(std::move(manifestPath)),
          moduleOverride(std::move(moduleOverride)),
          startupLevelOverride(std::move(startupLevelOverride)) {
    }

    void ProjectLayer::onAttach() {
        projectInstance.load(renderer, assets, manifestPath, moduleOverride, startupLevelOverride);
    }

    void ProjectLayer::onDetach() {
        projectInstance.unload();
    }

    void ProjectLayer::onUpdate(float deltaTime) {
        ATLAS_PROFILE_SCOPE("ProjectLayer::onUpdate");
        if (auto *scene = projectInstance.scene()) {
            scene->onUpdate(deltaTime);
        }
    }

    void ProjectLayer::onRender(FrameContext frameContext) {
        ATLAS_PROFILE_SCOPE("ProjectLayer::onRender");
        if (auto *scene = projectInstance.scene()) {
            scene->onRender(frameContext);
        }
    }

    void ProjectLayer::loadProject(std::filesystem::path newManifestPath, std::filesystem::path newModuleOverride, std::filesystem::path newStartupLevelOverride) {
        vkDeviceWaitIdle(renderer.device().device());
        manifestPath = std::move(newManifestPath);
        moduleOverride = std::move(newModuleOverride);
        startupLevelOverride = std::move(newStartupLevelOverride);
        projectInstance.load(renderer, assets, manifestPath, moduleOverride, startupLevelOverride);
    }
}
