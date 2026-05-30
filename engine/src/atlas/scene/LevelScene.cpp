#include "scene/LevelScene.hpp"

#include <utility>

#include <glm/gtc/matrix_transform.hpp>

#include "entity/Object.hpp"
#include "scene/LevelSerializer.hpp"

namespace Atlas {
    LevelScene::LevelScene(Renderer &renderer, AssetManager &assets, std::filesystem::path levelPath, std::filesystem::path projectRoot, std::filesystem::path assetRoot)
        : IScene(renderer), assets(assets), levelPath(std::move(levelPath)), projectRoot(std::move(projectRoot)), assetRoot(std::move(assetRoot)) {
        cameraSystem = std::make_unique<CameraSystem>(renderer.window());
        renderSystem = std::make_unique<RenderSystemV2>(renderer.device(), renderer, assets);
    }

    void LevelScene::onLoad(entt::registry &&loadedRegistry) {
        if (!levelPath.empty()) {
            IScene::onLoad(LevelSerializer::load(levelPath, assets, projectRoot, assetRoot));
        } else {
            IScene::onLoad(std::move(loadedRegistry));
        }

        ensureCamera();
        ensureSkybox();
        updateCameras();
        renderSystem->build(registry);
    }

    void LevelScene::onUpdate(float deltaTime) {
        ensureCamera();
        ensureSkybox();
        cameraSystem->update(registry, deltaTime, renderer.getAspectRatio());
        updateCameras();
        renderSystem->build(registry);
    }

    void LevelScene::onRender(FrameContext frameContext) {
        const entt::entity entity = activeCamera();
        if (entity != entt::null) {
            const CameraComponent &camera = registry.get<CameraComponent>(entity);
            renderSystem->render(frameContext, camera.camera.getData(), debugData());
        }
    }

    void LevelScene::onDelete() {
        IScene::onDelete();
    }

    void LevelScene::ensureCamera() {
        for (const entt::entity entity: registry.view<CameraComponent>()) {
            if (registry.all_of<EditorCameraComponent>(entity) || registry.all_of<TransientComponent>(entity)) {
                continue;
            }
            if (const auto *node = registry.try_get<SceneNodeComponent>(entity); node && node->deleted) {
                continue;
            }

            return;
        }

        const entt::entity cameraEntity = registry.create();
        SceneNodeComponent node{};
        node.name = "Camera";
        registry.emplace<SceneNodeComponent>(cameraEntity, std::move(node));

        auto &transform = registry.emplace<TransformComponent>(cameraEntity);
        transform.translation = {0.0f, 0.0f, 3.0f};

        registry.emplace<CameraComponent>(cameraEntity);
    }

    void LevelScene::ensureSkybox() {
        for (const entt::entity entity: registry.view<SkyboxComponent>()) {
            if (registry.all_of<TransientComponent>(entity)) {
                continue;
            }
            if (const auto *node = registry.try_get<SceneNodeComponent>(entity); node && node->deleted) {
                continue;
            }

            return;
        }

        const entt::entity skyboxEntity = registry.create();
        SceneNodeComponent node{};
        node.name = "Skybox";
        registry.emplace<SceneNodeComponent>(skyboxEntity, std::move(node));
        registry.emplace<SkyboxComponent>(skyboxEntity);
    }

    entt::entity LevelScene::activeCamera() {
        for (const entt::entity entity: registry.view<CameraComponent, EditorCameraComponent>()) {
            if (const auto *node = registry.try_get<SceneNodeComponent>(entity); node && (node->deleted || !node->visible)) {
                continue;
            }

            return entity;
        }

        for (const entt::entity entity: registry.view<CameraComponent>()) {
            if (const auto *node = registry.try_get<SceneNodeComponent>(entity); node && (node->deleted || !node->visible)) {
                continue;
            }

            if (registry.all_of<EditorCameraComponent>(entity)) {
                continue;
            }
            if (registry.all_of<TransientComponent>(entity)) {
                continue;
            }

            return entity;
        }

        return entt::null;
    }

    void LevelScene::updateCameras() {
        for (const entt::entity entity: registry.view<CameraComponent>()) {
            auto *transform = registry.try_get<TransformComponent>(entity);
            registry.patch<CameraComponent>(entity, [&](auto &camera) {
                if (transform) {
                    camera.camera.setViewYXZ(transform->translation, transform->rotation);
                }
                camera.camera.setPerspectiveProjection(glm::radians(50.0f), renderer.getAspectRatio(), 0.1f, 200.0f);
            });
        }
    }
}
