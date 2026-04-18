#include "OfficeScene.hpp"

#ifdef ATLAS_PLATFORM_DESKTOP
#include <imgui.h>
#endif

#include "core/Log.hpp"
#include "system/CameraSystem.hpp"
#include "system/RenderSystemV2.hpp"

#include "entity/Object.hpp"
#include "renderer/Renderer.hpp"

namespace Atlas {
    OfficeScene::OfficeScene(Renderer &renderer) : Scene(renderer) {
        cameraSystem = std::make_unique<CameraSystem>(renderer.getWindow());
        renderSystem = std::make_unique<RenderSystemV2>(renderer.getDevice(), renderer);
    }

    void OfficeScene::onLoad(entt::registry &&loadedRegistry) {
        this->registry = AssetManager::get().loadGltfAsScene("models/Cabinet_with_light3.glb");

        auto cameraEntity = registry.create();
        registry.emplace<TransformComponent>(cameraEntity);
        registry.emplace<CameraComponent>(cameraEntity, camera);

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
        camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 200.f);
    }

    void OfficeScene::onRender(VkCommandBuffer graphicsCmdBuffer) {
        static float irlMultiplier = 1.0f;
        static float exposureMultiplier = 1.0f;
#ifdef ATLAS_PLATFORM_DESKTOP
        ImGui::Begin("Debug Settings");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::SliderFloat("IRL Multiplier", &irlMultiplier, 0.01f, 10.0f);
        ImGui::SliderFloat("Exposure Multiplier", &exposureMultiplier, 0.0f, 5.0f);
        ImGui::End();
#endif

        const GlobalUbo ubo{
            camera.getData(),
            {irlMultiplier, exposureMultiplier}
        };

        renderSystem->render(graphicsCmdBuffer, renderer.getFrameIndex(), ubo);
    }

    void OfficeScene::onDelete() {
        Scene::onDelete();
    }
}
