#include "OfficeScene.hpp"


namespace Atlas {
    OfficeScene::OfficeScene(Renderer &renderer, AssetManager &assets) : IScene(renderer), assets(assets) {
        cameraSystem = std::make_unique<CameraSystem>(renderer.window());
        renderSystem = std::make_unique<RenderSystemV2>(renderer.device(), renderer, assets);
    }

    void OfficeScene::onLoad(entt::registry &&loadedRegistry) {
        IScene::onLoad(std::move(loadedRegistry));

        assets.importAsset("models/Cabinet_with_light3.glb", this->registry, entt::null);

        auto cameraEntity = registry.create();
        registry.emplace<TransformComponent>(cameraEntity);
        registry.emplace<CameraComponent>(cameraEntity);

        AssetHandle skybox = assets.loadCubemap("cubemaps/citrus_orchard_road_puresky_2k.hdr");
        AssetHandle irradiance = assets.loadCubemap("cubemaps/citrus_orchard_road_puresky_2k_irradiance.ktx2");
        AssetHandle prefilter = assets.loadCubemap("cubemaps/citrus_orchard_road_puresky_2k_prefilter.ktx2");

        auto skyboxEntity = registry.create();
        registry.emplace<SkyboxComponent>(skyboxEntity, skybox, irradiance, prefilter);

        renderSystem->build(registry);
    }

    void OfficeScene::onUpdate(float deltaTime) {
        float aspect = renderer.getAspectRatio();
        cameraSystem->update(registry, deltaTime, aspect);

        auto cameraView = registry.view<CameraComponent>();
        if (!cameraView.empty()) {
            const auto cameraEntity = *cameraView.begin();
            registry.patch<CameraComponent>(cameraEntity, [aspect](auto &component) {
                component.camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 200.f);
            });
        }
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
        IScene::onDelete();
    }
}
