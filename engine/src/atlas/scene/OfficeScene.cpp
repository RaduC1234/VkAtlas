#include "OfficeScene.hpp"

#ifdef ATLAS_PLATFORM_DESKTOP
#include <imgui.h>
#endif

#include "core/Log.hpp"
#include "system/CameraSystem.hpp"
#include "system/RenderSystemV2.hpp"
#include "system/SkyboxSystem.hpp"

#include "entity/Object.hpp"
#include "renderer/Renderer.hpp"

namespace Atlas {
    OfficeScene::OfficeScene(Renderer &renderer) : Scene(renderer) {
        uboBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
        for (auto &uboBuffer: uboBuffers) {
            uboBuffer = std::make_unique<Buffer>(
                renderer.getDevice(),
                sizeof(GlobalUbo),
                1,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VMA_MEMORY_USAGE_AUTO,
                renderer.getDevice().properties.limits.minUniformBufferOffsetAlignment,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
            );
            uboBuffer->map();
        }

        globalSetLayout = DescriptorSetLayout::Builder(renderer.getDevice())
                .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
                .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
                .build();

        globalPool = DescriptorPool::Builder(renderer.getDevice())
                .setMaxSets(SwapChain::MAX_FRAMES_IN_FLIGHT)
                .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SwapChain::MAX_FRAMES_IN_FLIGHT)
                .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SwapChain::MAX_FRAMES_IN_FLIGHT)
                .build();

        // Load BRDF LUT first so both bindings can be written together in one pass
        AssetHandle brdfHandle = AssetManager::get().loadTexture(
            "cubemaps/brdf_lut.hdr",
            VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
        );

        auto brdfTex = AssetManager::get().getTexture(brdfHandle);
        if (!brdfTex) {
            AT_ERROR("Failed to load BRDF LUT texture");
        }

        VkDescriptorImageInfo brdfImageInfo{};
        brdfImageInfo.sampler = brdfTex->getSampler();
        brdfImageInfo.imageView = brdfTex->getImageView();
        brdfImageInfo.imageLayout = brdfTex->getImageLayout();

        // Single build pass — writes both binding 0 (UBO) and binding 1 (BRDF LUT) together
        globalDescriptorSets.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
        for (int i = 0; i < static_cast<int>(globalDescriptorSets.size()); i++) {
            auto bufferInfo = uboBuffers[i]->descriptorInfo();
            const bool ok = DescriptorWriter(*globalSetLayout, *globalPool)
                    .writeBuffer(0, &bufferInfo)
                    .writeImage(1, &brdfImageInfo)
                    .build(globalDescriptorSets[i]);

            if (!ok) {
                AT_ERROR("Failed to build global descriptor set {}", i);
            }
        }

        cameraSystem = std::make_unique<CameraSystem>(renderer.getWindow());
        renderSystem = std::make_unique<RenderSystemV2>(renderer.getDevice(), renderer.getSwapChainRenderPass(), *globalSetLayout);
        skyboxSystem = std::make_unique<SkyboxSystem>(renderer.getDevice(), renderer.getSwapChainRenderPass(), *globalSetLayout);
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

    void OfficeScene::onRender(float deltaTime, float aspectRatio) {
        static float ibl = 0.03f;
        static float exposure = 1.0f;
#ifdef ATLAS_PLATFORM_DESKTOP
        ImGui::Begin("Debug Settings");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::SliderFloat("IBL Intensity", &ibl, 0.01, 0.5);
        ImGui::SliderFloat("Exposure", &exposure, 0.01f, 5.0f);
        ImGui::End();
#endif

        int frameIndex = renderer.getFrameIndex();

        const GlobalUbo ubo{
            camera.getData(),
            glm::vec4(0.02, 0.02, 0.03, 1.0),
            glm::vec3(-1.0f),
            ibl,
            exposure,
            0,0,0
        };
        uboBuffers[frameIndex]->uploadData(&ubo, sizeof(GlobalUbo));

        if (auto commandBuffer = renderer.getCurrentGraphicsCommandBuffer()) {
            skyboxSystem->render(registry, commandBuffer, globalDescriptorSets[frameIndex]);
            renderSystem->render(commandBuffer, globalDescriptorSets[frameIndex]);
        }
    }

    void OfficeScene::onDelete() {
        Scene::onDelete();
    }
}
