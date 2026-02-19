#include "PBRTestScene.hpp"

#include "entity/Object.hpp"
#include "renderer/Color.hpp"
#include <imgui.h>

namespace Atlas {
    struct alignas(16) GlobalUbo {
        glm::mat4 projection{1.0f};
        glm::mat4 view{1.0f};
        glm::vec4 ambientLightColor{0.1f, 0.1f, 0.1f, 0.2f}; // w can be used as intensity
        glm::vec3 lightPosition{0.0f}; // place the light in front of the plane (between camera and plane at z=5)
        float padding1{0.0f};
        glm::vec4 lightColor{1.0f, 1.0f, 1.0f, 1.0f};
    };

    PBRTestScene::PBRTestScene(Renderer &renderer): Scene(renderer) {
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
                .build();

        globalPool = DescriptorPool::Builder(renderer.getDevice())
                .setMaxSets(SwapChain::MAX_FRAMES_IN_FLIGHT)
                .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SwapChain::MAX_FRAMES_IN_FLIGHT)
                .build();

        globalDescriptorSets.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
        for (int i = 0; i < globalDescriptorSets.size(); i++) {
            auto bufferInfo = uboBuffers[i]->descriptorInfo();
            DescriptorWriter(*globalSetLayout, *globalPool)
                    .writeBuffer(0, &bufferInfo)
                    .build(globalDescriptorSets[i]);
        }

        cameraSystem = std::make_unique<CameraSystem>(renderer.getWindow());
        renderSystem = std::make_unique<RenderSystem>(renderer.getDevice(), renderer.getSwapChainRenderPass(), *globalSetLayout);
        skyboxSystem = std::make_unique<SkyboxSystem>(renderer.getDevice(), renderer.getSwapChainRenderPass(), *globalSetLayout);
    }

    void PBRTestScene::onLoad(entt::registry &&registry) {
        auto cameraEntity = this->registry.create();
        this->registry.emplace<TransformComponent>(cameraEntity, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f), glm::vec3(0.0f));
        this->registry.emplace<CameraComponent>(cameraEntity, camera);

        auto skyboxEntity = this->registry.create();
        this->registry.emplace<SkyboxComponent>(skyboxEntity, AssetManager::get().loadCubemap("cubemaps/citrus_orchard_road_puresky_2k.hdr"));

        auto plane = this->registry.create();
        this->registry.emplace<TransformComponent>(plane, glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(1.0f), glm::vec3(glm::radians(90.0f), 0.0f, 0.0f));
        this->registry.emplace<ModelComponent>(plane, AssetManager::get().createPlane(1, 1));
        this->registry.emplace<MaterialComponent>(
            plane,
            Color::white(),
            AssetManager::get().loadTexture("textures/brick_wall_wgukbfj_4k/Brick_Wall_wgukbfj_4K_BaseColor.jpg"),
            AssetManager::get().loadTexture("textures/brick_wall_wgukbfj_4k/Brick_Wall_wgukbfj_4K_Normal.jpg"),
            AssetManager::get().loadTexture("textures/brick_wall_wgukbfj_4k/Brick_Wall_wgukbfj_4K_Roughness.jpg")
        );
    }

    void PBRTestScene::onUpdate(float deltaTime) {
        float aspect = renderer.getAspectRatio();
        cameraSystem->update(registry, deltaTime, aspect);
        camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 200.f);
    }

    void PBRTestScene::onRender(float deltaTime, float aspectRatio) {
#ifdef _WIN32
        ImGui::Begin("Light Settings");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Separator();
        ImGui::DragFloat3("Light Position", &lightPosition.x, 0.1f);
        ImGui::ColorEdit3("Light Color", &lightColor.x);
        ImGui::DragFloat("Light Intensity", &lightIntensity, 0.1f, 0.0f, 100.0f);
        ImGui::ColorEdit4("Ambient Color", &ambientLightColor.x);
        ImGui::Checkbox("Render Skybox", &renderSkybox);
        ImGui::End();
#endif

        renderSystem->prepare(registry);

        int frameIndex = renderer.getFrameIndex();

        // Pack intensity into lightColor.w
        glm::vec4 packedLightColor = lightColor;
        packedLightColor.w = lightIntensity;

        const GlobalUbo ubo{
            camera.getProjection(),
            camera.getView(),
            ambientLightColor,
            lightPosition,
            0.0f,
            packedLightColor
        };
        uboBuffers[frameIndex]->uploadData(&ubo, sizeof(GlobalUbo));

        if (auto commandBuffer = renderer.getCurrentCommandBuffer()) {
            if (renderSkybox) {
                skyboxSystem->render(registry, commandBuffer, globalDescriptorSets[frameIndex]);
            }
            renderSystem->render(registry, commandBuffer, globalDescriptorSets[frameIndex]);
        }
    }

    void PBRTestScene::onDelete() {
        Scene::onDelete();
    }
}
