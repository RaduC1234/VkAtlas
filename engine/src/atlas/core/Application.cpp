#include "Application.hpp"

// std
#include <chrono>

#include "AssetManager.hpp"

#include "system/RenderSystem.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include "entity/Object.hpp"
#include "renderer/ImGuiLayer.hpp"
#include "system/CameraSystem.hpp"

namespace Atlas {
    Application::Application(const ApplicationSpecification &spec) : specification(spec) {
        // Load Systems
        AssetManager::init(spec.pNativeApp);

        loadGameObjects();
    }

    Application::~Application() {
    }

    void Application::run() {
        ImGuiLayer imGui{device, *window, renderer.getSwapChainRenderPass(), static_cast<uint32_t>(renderer.getImageCount()) };
        CameraSystem cameraSystem{*window};
        RenderSystem renderSystem{device, renderer.getSwapChainRenderPass()};
        Camera camera{};
        //camera.setViewDirection(glm::vec3(0.0f), glm::vec3(0.5f, 0.0f, 1.0f));

        auto currentTime = std::chrono::high_resolution_clock::now();

        auto cameraObj = registry.create();
        auto &transform = registry.emplace<Transform>(cameraObj);
        auto &cameraComponent = registry.emplace<CameraComponent>(cameraObj, camera);

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
                ImGui::Text("Test");
                ImGui::End();

                renderer.beginSwapChainRenderPass(commandBuffer);
                cameraSystem.update(registry, frameTime);
                renderSystem.update(registry, commandBuffer, camera);

                imGui.endFrame(commandBuffer);

                renderer.endSwapChainRenderPass(commandBuffer);
                renderer.endFrame();
            }
        }

        vkDeviceWaitIdle(device.device());
    }

    void Application::loadGameObjects() {
        std::shared_ptr<Mesh> model = Mesh::createModelFromFileObj(device, "assets/models/flat_vase.obj");

        auto gameObject = registry.create();
        auto &transform = registry.emplace<Transform>(gameObject);
        transform.translation = {0.0f, 0.0f, 1.0f};
        transform.scale = glm::vec3{1.0f};

        registry.emplace<Material>(gameObject, Color::white());
        registry.emplace<ModelComponent>(gameObject, std::move(model));
    }
} // namespace
