#include "Scene.hpp"

#include "system/CameraSystem.hpp"
#include "system/RenderSystem.hpp"

#include <stdexcept>

#include "entity/Object.hpp"

namespace Atlas {
    Scene::Scene(Renderer &renderer) : renderer(renderer) {
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

    void Scene::onLoad(entt::registry &&loadedRegistry) {
        this->registry = AssetManager::get().loadGltfAsScene("models/Cabinet.glb");

        auto cameraEntity = registry.create();
        registry.emplace<TransformComponent>(cameraEntity);
        registry.emplace<CameraComponent>(cameraEntity, camera);

        AssetHandle skybox = AssetManager::get().loadCubemap("cubemaps/citrus_orchard_road_puresky_2k.hdr");
        auto skyboxEntity = registry.create();
        registry.emplace<SkyboxComponent>(skyboxEntity, skybox);
    }

    void Scene::onUpdate(float deltaTime) {
        float aspect = renderer.getAspectRatio();
        cameraSystem->update(registry, deltaTime, aspect);
        camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 200.f);
    }

    void Scene::onRender(float deltaTime, float aspectRatio) {
#ifdef _WIN32
        ImGui::Begin("Debug Settings");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::End();
#endif
        renderSystem->prepareTextures(registry);

        int frameIndex = renderer.getFrameIndex();

        const GlobalUbo ubo{
            camera.getProjection(),
            camera.getView(),
            glm::vec4(1.0f, 1.0f, 1.0f, 1.f), // ambient color
            glm::vec3(-1.0f), // light position
            0.0f,
            glm::vec4(1.0f) // light color
        };
        uboBuffers[frameIndex]->uploadData(&ubo, sizeof(GlobalUbo));

        if (auto commandBuffer = renderer.getCurrentCommandBuffer()) {
            skyboxSystem->render(registry, commandBuffer, globalDescriptorSets[frameIndex]);
            renderSystem->render(registry, commandBuffer, globalDescriptorSets[frameIndex]);
        }
    }

    void Scene::onDelete() {
        registry.clear();
    }

    Scene Scene::loadSceneFromJson(const std::string &json) {
        throw std::runtime_error("Scene::loadSceneFromJson not yet implemented");
    }

    std::string Scene::saveSceneToJson(const Scene &scene) {
        throw std::runtime_error("Scene::saveSceneToJson not yet implemented");
    }
}
