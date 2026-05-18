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
        auto &cameraTransform = registry.emplace<TransformComponent>(cameraEntity);
        cameraTransform.translation = {0.0f, 1.5f, -8.0f};
        auto &camera = registry.emplace<CameraComponent>(cameraEntity);
        camera.camera.setViewYXZ(cameraTransform.translation, cameraTransform.rotation);
        camera.camera.setPerspectiveProjection(glm::radians(50.f), renderer.getAspectRatio(), 0.1f, 200.f);

        AssetHandle<Cubemap> skybox;
        AssetHandle<Cubemap> irradiance;
        AssetHandle<Cubemap> prefilter;

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
            const auto *transform = registry.try_get<TransformComponent>(cameraEntity);
            registry.patch<CameraComponent>(cameraEntity, [aspect, transform](auto &component) {
                if (transform) {
                    component.camera.setViewYXZ(transform->translation, transform->rotation);
                }
                component.camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 200.f);
            });
        }

        bool resourcesReady = true;

        for (auto entity: registry.view<ModelComponent>()) {
            const auto &model = registry.get<ModelComponent>(entity);
            if (model.meshHandle.valid() && !model.meshHandle.isReady()) {
                resourcesReady = false;
                break;
            }
        }

        if (resourcesReady) {
            auto textureReady = [](const AssetHandle<Texture> &handle) {
                return !handle.valid() || handle.isReady();
            };

            for (auto entity: registry.view<MaterialComponent>()) {
                const auto &material = registry.get<MaterialComponent>(entity);
                if (!textureReady(material.albedoTexture) ||
                    !textureReady(material.normalMap) ||
                    !textureReady(material.metallicRoughnessMap) ||
                    !textureReady(material.ambientOcclusion)) {
                    resourcesReady = false;
                    break;
                }
            }
        }

        if (resourcesReady) {
            auto cubemapReady = [](const AssetHandle<Cubemap> &handle) {
                return !handle.valid() || handle.isReady();
            };

            for (auto entity: registry.view<SkyboxComponent>()) {
                const auto &skybox = registry.get<SkyboxComponent>(entity);
                if (!cubemapReady(skybox.skyboxHandle) ||
                    !cubemapReady(skybox.irradianceHandle) ||
                    !cubemapReady(skybox.prefilterHandle)) {
                    resourcesReady = false;
                    break;
                }
            }
        }

        if (!renderGraphReady || !resourcesReady) {
            renderSystem->build(registry);
            renderGraphReady = resourcesReady;
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
