#include "Application.hpp"

// std
#include <chrono>

#include "AssetManager.hpp"
#include "system/RenderSystem.hpp"
#include "system/CameraSystem.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <imgui.h>
#include <glm/glm.hpp>

#include "entity/Object.hpp"
#include "renderer/ImGuiLayer.hpp"
#include "renderer/Sampler.hpp"

namespace Atlas {
    Application::Application(const ApplicationSpecification &spec) : specification(spec) {
        AssetManager::init(spec.pNativeApp);
        this->window->setWindowIcon("assets/textures/android_robot.png");
        this->window->setTheme(true);

        loadGameObjects();
    }

    Application::~Application() {
    }

    void Application::run() {
        ImGuiLayer imGui{device, *window, renderer.getSwapChainRenderPass(), static_cast<uint32_t>(renderer.getImageCount())};
        CameraSystem cameraSystem{*window};
        RenderSystem renderSystem{device, renderer.getSwapChainRenderPass()};

        renderSystem.registerMaterials(registry);

        Camera camera{};
        auto currentTime = std::chrono::high_resolution_clock::now();

        auto cameraObj = registry.create();
        registry.emplace<TransformComponent>(cameraObj);
        registry.emplace<CameraComponent>(cameraObj, camera);

        while (!window->shouldClose()) {
            window->pollEvents();

            auto newTime = std::chrono::high_resolution_clock::now();
            float frameTime = std::chrono::duration_cast<std::chrono::duration<float>>(newTime - currentTime).count();
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

                // Update UBO data
                renderSystem.updateUBO(
                    frameIndex,
                    camera.getProjection(),
                    camera.getView(),
                    glm::vec4(1.0f, 1.0f, 1.0f, 0.005f),  // ambient color
                    glm::vec3(-1.0f),                       // light position
                    glm::vec4(1.0f)                         // light color
                );

                // Render
                renderer.beginSwapChainRenderPass(commandBuffer);
                cameraSystem.update(registry, frameTime);
                renderSystem.render(registry, commandBuffer, frameIndex);

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

        registry.emplace<MaterialComponent>(gameObject0);
        registry.emplace<ModelComponent>(gameObject0, std::move(model0));


        std::shared_ptr<Mesh> model1 = Mesh::createModelFromFileObj(device, "assets/models/smooth_vase.obj");

        auto gameObject1 = registry.create();
        auto &transform1 = registry.emplace<TransformComponent>(gameObject1);
        transform1.translation = {0.0f, 0.5f, 0.25f};
        transform1.scale = glm::vec3{1.0f};

        registry.emplace<MaterialComponent>(gameObject1);
        registry.emplace<ModelComponent>(gameObject1, std::move(model1));


        std::shared_ptr<Mesh> model2 = Mesh::createSphere(device, 0.1f);

        auto gameObject2 = registry.create();
        auto &transform2 = registry.emplace<TransformComponent>(gameObject2);
        transform2.translation = glm::vec3{0.0f};
        transform2.scale = glm::vec3{1.0};

        registry.emplace<MaterialComponent>(gameObject2);
        registry.emplace<ModelComponent>(gameObject2, std::move(model2));


        std::shared_ptr<Mesh> model3 = Mesh::createModelFromFileObj(device, "assets/models/quad.obj");

        auto gameObject3 = registry.create();
        auto &transform3 = registry.emplace<TransformComponent>(gameObject3);
        transform3.translation = {0, 0.5f, 0.0f};
        transform3.scale = glm::vec3{2.0f, 1.0f, 2.0f};

        // Create brick texture for the quad
        auto brickTexture = Sampler::create(device, "assets/textures/Brick_4K_BaseColor.jpg");

        auto &material3 = registry.emplace<MaterialComponent>(gameObject3);
        material3.albedoTexture = brickTexture;

        registry.emplace<ModelComponent>(gameObject3, std::move(model3));
    }
} // namespace
