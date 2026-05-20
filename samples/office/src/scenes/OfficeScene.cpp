#include "OfficeScene.hpp"

#include <chrono>
#include <exception>

#include "core/Log.hpp"


namespace Atlas {
    OfficeScene::OfficeScene(Renderer &renderer, AssetManager &assets) : IScene(renderer), assets(assets) {
        cameraSystem = std::make_unique<CameraSystem>(renderer.window());
        renderSystem = std::make_unique<RenderSystemV2>(renderer.device(), renderer, assets);
    }

    void OfficeScene::onLoad(entt::registry &&loadedRegistry) {
        IScene::onLoad(std::move(loadedRegistry));

        importComplete = false;

        AT_INFO("OfficeScene: queued async import");

        auto cameraEntity = registry.create();
        auto &cameraTransform = registry.emplace<TransformComponent>(cameraEntity);
        cameraTransform.translation = {0.0f, 1.5f, -8.0f};
        auto &camera = registry.emplace<CameraComponent>(cameraEntity);
        camera.camera.setViewYXZ(cameraTransform.translation, cameraTransform.rotation);
        camera.camera.setPerspectiveProjection(glm::radians(50.f), renderer.getAspectRatio(), 0.1f, 200.f);

        auto skybox = assets.store<Cubemap>("cubemaps/citrus_orchard_road_puresky_2k.hdr");
        auto irradiance = assets.store<Cubemap>("cubemaps/citrus_orchard_road_puresky_2k_irradiance.ktx2");
        auto prefilter = assets.store<Cubemap>("cubemaps/citrus_orchard_road_puresky_2k_prefilter.ktx2");

        auto skyboxEntity = registry.create();
        registry.emplace<SkyboxComponent>(skyboxEntity, skybox, irradiance, prefilter);

        renderSystem->build(registry, debugData().viewMode);

        AssetManager *assetManager = &assets;
        importFuture = renderer.device().executor().submit([assetManager] {
            entt::registry importedRegistry;
            assetManager->importAsset("models/Cabinet_with_light3.glb", importedRegistry, entt::null);
            return importedRegistry;
        });
    }

    void OfficeScene::onUpdate(float deltaTime) {
        float aspect = renderer.getAspectRatio();
        cameraSystem->update(registry, deltaTime, aspect);

        auto cameraView = registry.view<CameraComponent>();
        if (!cameraView.empty()) {
            const auto cameraEntity = *cameraView.begin();
            const auto *transform = registry.try_get<TransformComponent>(cameraEntity);
            registry.patch<CameraComponent>(cameraEntity, [aspect, transform](auto &component) {
                if (transform) {
                    component.camera.setViewYXZ(transform->translation, transform->rotation);
                }
                component.camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 200.f);
            });
        }

        if (!importComplete) {
            if (!importFuture.valid() || importFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                renderSystem->build(registry, debugData().viewMode);
                return;
            }

            try {
                auto importedRegistry = importFuture.get();

                auto cameraView = registry.view<TransformComponent, CameraComponent>();
                if (cameraView.begin() != cameraView.end()) {
                    const auto cameraEntity = *cameraView.begin();
                    const auto &cameraTransform = registry.get<TransformComponent>(cameraEntity);
                    auto &camera = registry.get<CameraComponent>(cameraEntity);

                    const auto newCameraEntity = importedRegistry.create();
                    importedRegistry.emplace<TransformComponent>(newCameraEntity, cameraTransform);
                    importedRegistry.emplace<CameraComponent>(newCameraEntity, camera);
                }

                auto skyboxView = registry.view<SkyboxComponent>();
                if (!skyboxView.empty()) {
                    importedRegistry.emplace<SkyboxComponent>(importedRegistry.create(), registry.get<SkyboxComponent>(*skyboxView.begin()));
                }

                registry = std::move(importedRegistry);
            } catch (const std::exception &e) {
                AT_ERROR("OfficeScene: import failed: {}", e.what());
                importComplete = true;
                return;
            }

            importComplete = true;
            AT_INFO("OfficeScene: async import completed");
        }

        renderSystem->build(registry, debugData().viewMode);
    }

    void OfficeScene::onRender(FrameContext frameContext) {
        auto cameraView = registry.view<CameraComponent>();
        if (cameraView.empty()) {
            return;
        }

        const auto cameraEntity = *cameraView.begin();
        const auto &camera = cameraView.get<CameraComponent>(cameraEntity).camera;

        auto debugDt = debugData();
        renderSystem->render(
            frameContext,
            camera.getData(),
            debugDt
        );
    }

    void OfficeScene::onDelete() {
        if (importFuture.valid()) {
            importFuture.wait();
        }

        IScene::onDelete();
    }
}
