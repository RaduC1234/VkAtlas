#include "Application.hpp"

// std
#include <chrono>

#include "asset/AssetManager.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#ifdef _WIN32
#include <imgui.h>
#endif

#include "entity/Object.hpp"
#include "renderer/ImGuiLayer.hpp"

namespace Atlas {
    Application::Application(const ApplicationSpecification &spec) : specification(spec) {
        AssetManager::create(this->device, spec.pNativeApp);
        this->window->setWindowIcon("assets/icons/android_robot.png");
        this->window->setTheme(Theme::DARK);

        currentScene = std::make_unique<Scene>(renderer);

        auto sceneRegistry = AssetManager::get().loadGltfAsScene("models/Cabinet.glb");

        if (currentScene) {
            currentScene->onLoad(std::move(sceneRegistry));
        }
    }

    Application::~Application() {
        if (currentScene) {
            currentScene->onDelete();
        }
        AssetManager::destroy();
    }

    void Application::run() {
        ImGuiLayer imGui{device, *window, renderer.getSwapChainRenderPass(), static_cast<uint32_t>(renderer.getImageCount())};


        auto currentTime = std::chrono::high_resolution_clock::now();

        while (!window->shouldClose()) {
            window->pollEvents();

            auto newTime = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration_cast<std::chrono::duration<float> >(newTime - currentTime).count();
            currentTime = newTime;

            float aspect = renderer.getAspectRatio();


            if (auto commandBuffer = renderer.beginFrame()) {
                // Update scene
                if (currentScene) {
                    currentScene->onUpdate(deltaTime);
                }

                // ImGui frame
                imGui.beginFrame();

#ifdef _WIN32
                ImGui::Begin("Debug Settings");
                ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
                ImGui::End();
#endif
                renderer.beginSwapChainRenderPass(commandBuffer);

                if (currentScene) {
                    currentScene->onRender(deltaTime, aspect);
                }

                imGui.endFrame(commandBuffer);

                renderer.endSwapChainRenderPass(commandBuffer);
                renderer.endFrame();
            }
        }

        vkDeviceWaitIdle(device.device());
    }
} // namespace
