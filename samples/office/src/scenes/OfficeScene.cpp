#include "OfficeScene.hpp"

#include "core/Log.hpp"

namespace Atlas {
    OfficeScene::OfficeScene(Renderer &renderer, AssetManager &assets) : IScene(renderer), assets(assets) {
        cameraSystem = std::make_unique<CameraSystem>(renderer.window());
        renderSystem = std::make_unique<RenderSystemV2>(renderer.device(), renderer, assets);
    }

    void OfficeScene::onLoad(entt::registry &&loadedRegistry) {
        IScene::onLoad(std::move(loadedRegistry));

        auto cameraEntity = registry.create();
        auto &cameraTransform = registry.emplace<TransformComponent>(cameraEntity);
        cameraTransform.translation = {0.0f, 0.0f, 0.0f};
        auto &camera = registry.emplace<CameraComponent>(cameraEntity);
        camera.camera.setViewYXZ(cameraTransform.translation, cameraTransform.rotation);
        camera.camera.setPerspectiveProjection(glm::radians(50.f), renderer.getAspectRatio(), 0.1f, 200.f);

        auto skybox = assets.store<Cubemap>("cubemaps/citrus_orchard_road_puresky_2k.hdr");
        auto irradiance = assets.store<Cubemap>("cubemaps/citrus_orchard_road_puresky_2k_irradiance.ktx2");
        auto prefilter = assets.store<Cubemap>("cubemaps/citrus_orchard_road_puresky_2k_prefilter.ktx2");

        auto skyboxEntity = registry.create();
        registry.emplace<SkyboxComponent>(skyboxEntity, skybox, irradiance, prefilter);

        renderSystem->build(registry);
    }

    void OfficeScene::onUpdate(float deltaTime) {
        cameraSystem->update(registry, deltaTime, renderer.getAspectRatio());

        for (auto entity: registry.view<CameraComponent>()) {
            auto *t = registry.try_get<TransformComponent>(entity);
            registry.patch<CameraComponent>(entity, [&](auto &c) {
                if (t) c.camera.setViewYXZ(t->translation, t->rotation);
                c.camera.setPerspectiveProjection(glm::radians(50.f), renderer.getAspectRatio(), 0.1f, 200.f);
            });
        }

        renderSystem->build(registry);
    }

    void OfficeScene::onRender(FrameContext frameContext) {
        for (auto entity: registry.view<CameraComponent>()) {
            const auto &camera = registry.get<CameraComponent>(entity).camera;
            renderSystem->render(frameContext, camera.getData(), debugData());
            return;
        }
    }

    void OfficeScene::onDelete() {
        IScene::onDelete();
    }
}
