#include "OfficeScene.hpp"

#include <imgui.h>

#include "core/Log.hpp"
#include "system/CameraSystem.hpp"
#include "system/RenderSystemV2.hpp"

#include "entity/Object.hpp"
#include "renderer/Renderer.hpp"

namespace Atlas {
    OfficeScene::OfficeScene(Renderer &renderer) : IScene(renderer) {
        cameraSystem = std::make_unique<CameraSystem>(renderer.window());
        renderSystem = std::make_unique<RenderSystemV2>(renderer.device(), renderer);
    }

    void OfficeScene::onLoad(entt::registry &&loadedRegistry) {
        AssetManager::get().importAsset("models/Cabinet_with_light3.glb", this->registry, entt::null);

        auto cameraEntity = registry.create();
        registry.emplace<TransformComponent>(cameraEntity);
        registry.emplace<CameraComponent>(cameraEntity);

        AssetHandle skybox = AssetManager::get().loadCubemap("cubemaps/citrus_orchard_road_puresky_2k.hdr");
        AssetHandle irradiance = AssetManager::get().loadCubemap("cubemaps/citrus_orchard_road_puresky_2k_irradiance.ktx2");
        AssetHandle prefilter = AssetManager::get().loadCubemap("cubemaps/citrus_orchard_road_puresky_2k_prefilter.ktx2");

        auto skyboxEntity = registry.create();
        registry.emplace<SkyboxComponent>(skyboxEntity, skybox, irradiance, prefilter);

        renderSystem->build(registry);
    }

    void OfficeScene::onUpdate(float deltaTime) {
        float aspect = renderer.getAspectRatio();
        cameraSystem->update(registry, deltaTime, aspect);

        auto cameraView = registry.view<CameraComponent>();
        if (!cameraView.empty()) {
            const auto e = *cameraView.begin();
            registry.patch<CameraComponent>(e, [aspect](auto &cc){
                cc.camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 200.f);
            });
        }

    }

    void OfficeScene::onRender(FrameContext frameContext) {
        static float irlMultiplier = 1.0f;
        static float exposureMultiplier = 1.0f;
        static int viewModeIndex = static_cast<int>(ViewMode::LIT);

        ImGui::Begin("Debug Settings");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::SliderFloat("IRL Multiplier", &irlMultiplier, 0.01f, 10.0f);
        ImGui::SliderFloat("Exposure Multiplier", &exposureMultiplier, 0.0f, 5.0f);
        ImGui::Combo("View Mode", &viewModeIndex, "Lit\0Unlit\0Lighting Only\0Path Tracing\0");
        ImGui::End();

        const auto viewMode = static_cast<ViewMode>(viewModeIndex);

        auto cameraView = registry.view<CameraComponent>();
        if (cameraView.empty()) {
            return;
        }

        const auto cameraEntity = *cameraView.begin();
        const auto &camera = cameraView.get<CameraComponent>(cameraEntity).camera;

        renderSystem->render(
            frameContext,
            camera.getData(),
            {irlMultiplier, exposureMultiplier, viewMode, 0.0f}
        );
    }

    void OfficeScene::onDelete() {
        IScene::onDelete();
    }
}
