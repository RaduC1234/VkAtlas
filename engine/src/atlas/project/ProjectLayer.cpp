#include "project/ProjectLayer.hpp"

#include <utility>

namespace Atlas {
    ProjectLayer::ProjectLayer(Renderer &renderer, AssetManager &assets, std::filesystem::path manifestPath, std::filesystem::path moduleOverride)
        : Layer("ProjectLayer"), renderer(renderer), assets(assets), manifestPath(std::move(manifestPath)), moduleOverride(std::move(moduleOverride)) {
    }

    void ProjectLayer::onAttach() {
        projectInstance.load(renderer, assets, manifestPath, moduleOverride);
    }

    void ProjectLayer::onDetach() {
        projectInstance.unload();
    }

    void ProjectLayer::onUpdate(float deltaTime) {
        if (auto *scene = projectInstance.scene()) {
            scene->onUpdate(deltaTime);
        }
    }

    void ProjectLayer::onRender(FrameContext frameContext) {
        if (auto *scene = projectInstance.scene()) {
            scene->onRender(frameContext);
        }
    }
}
