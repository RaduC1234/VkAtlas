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
        uboBuffers.resize(WindowSwapChain::MAX_FRAMES_IN_FLIGHT);
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

        // Pool must provide enough combined image sampler descriptors for each frame-in-flight
        globalPool = DescriptorPool::Builder(renderer.getDevice())
                .setMaxSets(ISwapChain::MAX_FRAMES_IN_FLIGHT)
                .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ISwapChain::MAX_FRAMES_IN_FLIGHT)
                .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, ISwapChain::MAX_FRAMES_IN_FLIGHT)
                .build();

        globalDescriptorSets.resize(WindowSwapChain::MAX_FRAMES_IN_FLIGHT);
        for (int i = 0; i < globalDescriptorSets.size(); i++) {
            auto bufferInfo = uboBuffers[i]->descriptorInfo();
            DescriptorWriter(*globalSetLayout, *globalPool)
                    .writeBuffer(0, &bufferInfo)
                    .build(globalDescriptorSets[i]);
        }

        AssetHandle brdfHandle = AssetManager::get().loadTexture("cubemaps/brdf_lut.hdr", VK_FORMAT_R32G32B32_SFLOAT, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        if (auto brdfSampler = AssetManager::get().getTexture(brdfHandle)) {
            VkDescriptorImageInfo brdfImageInfo{};
            brdfImageInfo.sampler = brdfSampler->getSampler();
            brdfImageInfo.imageView = brdfSampler->getImageView();
            brdfImageInfo.imageLayout = brdfSampler->getImageLayout();

            for (size_t i = 0; i < globalDescriptorSets.size(); ++i) {
                DescriptorWriter(*globalSetLayout, *globalPool)
                        .writeImage(1, &brdfImageInfo)
                        .overwrite(globalDescriptorSets[i]);
            }
        } else {
            AT_WARN("Failed to get BRDF sampler asset after loading: {}", AssetManager::get().getPath(brdfHandle));
        }

        cameraSystem = std::make_unique<CameraSystem>(renderer.getWindow());
        renderSystem = std::make_unique<RenderSystemV2>(renderer.getDevice(), renderer.getSwapChainRenderPass(), *globalSetLayout);
        skyboxSystem = std::make_unique<SkyboxSystem>(renderer.getDevice(), renderer.getSwapChainRenderPass(), *globalSetLayout);
    }

    void OfficeScene::onLoad(entt::registry &&loadedRegistry) {
        this->registry = AssetManager::get().loadGltfAsScene("models/Cabinet_with_light2.glb");

        auto cameraEntity = registry.create();
        registry.emplace<TransformComponent>(cameraEntity);
        registry.emplace<CameraComponent>(cameraEntity, camera);

        AssetHandle skybox = AssetManager::get().loadCubemap("cubemaps/citrus_orchard_road_puresky_2k.hdr");
        auto skyboxEntity = registry.create();
        registry.emplace<SkyboxComponent>(skyboxEntity, skybox);

        renderSystem->build(registry);
    }

    void OfficeScene::onUpdate(float deltaTime) {
        float aspect = renderer.getAspectRatio();
        cameraSystem->update(registry, deltaTime, aspect);
        camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 200.f);
    }

    void OfficeScene::onRender(float deltaTime, float aspectRatio) {
#ifdef ATLAS_PLATFORM_DESKTOP
        ImGui::Begin("Debug Settings");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::End();
#endif

        int frameIndex = renderer.getFrameIndex();

        const GlobalUbo ubo{
            camera.getData(),
            glm::vec4(0.02, 0.02, 0.03, 1.0), // ambient color
            glm::vec3(-1.0f), // light position
            0.0f,
            glm::vec4(1.0f) // light color
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
