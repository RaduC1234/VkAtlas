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
        renderSystem = std::make_unique<RenderSystemV2>(renderer.getDevice(), renderer.getSwapChainRenderPass());
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

    void OfficeScene::onRender(ImGuiLayer& imguiLayer) {
#ifdef ATLAS_PLATFORM_DESKTOP
        ImGui::Begin("Debug Settings");
        ImGui::End();
#endif

        const GlobalUbo ubo{
            camera.getData(),
            glm::vec4(0.02, 0.02, 0.03, 1.0)
        };
        renderSystem->render(renderer, ubo, imguiLayer);
    }

    void OfficeScene::onDelete() {
        Scene::onDelete();
    }
}
