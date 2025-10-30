#include "Application.hpp"

// std
#include <chrono>

#include "AssetManager.hpp"

#include "system/RenderSystem.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <imgui.h>
#include <glm/glm.hpp>

#include "entity/Object.hpp"
#include "renderer/ImGuiLayer.hpp"
#include "system/CameraSystem.hpp"

namespace Atlas {
    struct alignas(16) GlobalUbo {
        glm::mat4 projection{1.0f};
        glm::mat4 view{1.0f};
        glm::vec4 ambientColor{1.0f, 1.0f, 1.0f, 0.002f};
        glm::vec3 lightPosition{-1.0f};
        glm::vec4 lightColor{1.0f};

    };

    Application::Application(const ApplicationSpecification &spec) : specification(spec) {
        // Load Systems
        AssetManager::init(spec.pNativeApp);

        globalPool = DescriptorPool::Builder(device)
                .setMaxSets(SwapChain::MAX_FRAMES_IN_FLIGHT)
                .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SwapChain::MAX_FRAMES_IN_FLIGHT)
                .build();

        loadGameObjects();
    }

    Application::~Application() {
    }

    void Application::run() {
        std::vector<std::shared_ptr<Buffer> > uboBuffers{SwapChain::MAX_FRAMES_IN_FLIGHT};
        for (auto &uboBuffer: uboBuffers) {
            uboBuffer = std::make_unique<Buffer>(
                device,
                sizeof(GlobalUbo),
                1,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VMA_MEMORY_USAGE_AUTO,
                device.properties.limits.minUniformBufferOffsetAlignment,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
            );
            uboBuffer->map();
        }
        auto globalSetLayout = DescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
                .build();

        std::vector<VkDescriptorSet> globalDescriptorSets(SwapChain::MAX_FRAMES_IN_FLIGHT);

        for (int i = 0; i < globalDescriptorSets.size(); i++) {
            auto bufferInfo = uboBuffers[i]->descriptorInfo();
            DescriptorWriter(*globalSetLayout, *globalPool)
                    .writeBuffer(0, &bufferInfo)
                    .build(globalDescriptorSets[i]);
        }


        ImGuiLayer imGui{device, *window, renderer.getSwapChainRenderPass(), static_cast<uint32_t>(renderer.getImageCount())};
        CameraSystem cameraSystem{*window};
        RenderSystem renderSystem{device, renderer.getSwapChainRenderPass(), globalSetLayout->getDescriptorSetLayout()};
        Camera camera{};
        //camera.setViewDirection(glm::vec3(0.0f), glm::vec3(0.5f, 0.0f, 1.0f));

        auto currentTime = std::chrono::high_resolution_clock::now();

        auto cameraObj = registry.create();
        registry.emplace<TransformComponent>(cameraObj);
        registry.emplace<CameraComponent>(cameraObj, camera);

        while (!window->shouldClose()) {
            window->pollEvents();

            auto newTime = std::chrono::high_resolution_clock::now();
            float frameTime = std::chrono::duration_cast<std::chrono::duration<float> >(newTime - currentTime).count();
            currentTime = newTime;

            float aspect = renderer.getAspectRatio();
            //camera.setOrthographicProjection(-aspect, aspect, -1, 1, -1, 1);
            camera.setPerspectiveProjection(glm::radians(50.0f), aspect, 0.1f, 100.0f);


            if (auto commandBuffer = renderer.beginFrame()) {
                imGui.beginFrame();

                ImGui::Begin("Debug Settings");
                ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
                ImGui::End();

                int frameIndex = renderer.getFrameIndex();
                // update
                GlobalUbo ubo{};
                ubo.projection = camera.getProjection();
                ubo.view = camera.getView();

                uboBuffers[frameIndex]->uploadData(&ubo, sizeof(GlobalUbo));

                // render
                renderer.beginSwapChainRenderPass(commandBuffer);
                cameraSystem.update(registry, frameTime);
                renderSystem.update(registry, commandBuffer, camera, globalDescriptorSets[frameIndex]);

                imGui.endFrame(commandBuffer);

                renderer.endSwapChainRenderPass(commandBuffer);
                renderer.endFrame();
            }
        }

        vkDeviceWaitIdle(device.device());
    }

    void Application::loadGameObjects() {
        std::shared_ptr<Mesh> model0 = Mesh::createModelFromFileObj(device, "assets/models/flat_vase.obj");

        auto gameObject0 = registry.create();
        auto &transform0 = registry.emplace<TransformComponent>(gameObject0);
        transform0.translation = {0.0f, 0.5f, -0.25f};
        transform0.scale = glm::vec3{1.0f};

        registry.emplace<MaterialComponent>(gameObject0, Color::white());
        registry.emplace<ModelComponent>(gameObject0, std::move(model0));


        std::shared_ptr<Mesh> model1 = Mesh::createModelFromFileObj(device, "assets/models/smooth_vase.obj");

        auto gameObject1 = registry.create();
        auto &transform1 = registry.emplace<TransformComponent>(gameObject1);
        transform1.translation = {0.0f, 0.5f, 0.25f};
        transform1.scale = glm::vec3{1.0f};

        registry.emplace<MaterialComponent>(gameObject1, Color::white());
        registry.emplace<ModelComponent>(gameObject1, std::move(model1));

        
        std::shared_ptr<Mesh> model2 = Mesh::createSphere(device, 0.1f);

        auto gameObject2 = registry.create();
        auto &transform2 = registry.emplace<TransformComponent>(gameObject2);
        transform2.translation = glm::vec3{0.0f};
        transform2.scale = glm::vec3{1.0};

        registry.emplace<MaterialComponent>(gameObject2, Color::white());
        registry.emplace<ModelComponent>(gameObject2, std::move(model2));


        std::shared_ptr<Mesh> model3 = Mesh::createModelFromFileObj(device, "assets/models/quad.obj");

        auto gameObject3 = registry.create();
        auto &transform3 = registry.emplace<TransformComponent>(gameObject3);
        transform3.translation = {0, 0.5f, 0.0f};
        transform3.scale = glm::vec3{2.0f, 1.0f, 2.0f};

        registry.emplace<MaterialComponent>(gameObject3, Color::white());
        registry.emplace<ModelComponent>(gameObject3, std::move(model3));
    }
} // namespace
