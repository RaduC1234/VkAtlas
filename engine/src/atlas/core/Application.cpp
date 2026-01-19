#include "Application.hpp"

// std
#include <chrono>

#include "asset/AssetManager.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "renderer/ImGuiLayer.hpp"
#include "scene/OfficeScene.hpp"
#include "scene/PBRTestScene.hpp"

namespace Atlas {
    Application::Application(const ApplicationSpecification &spec) : specification(spec) {
        AssetManager::create(this->device, spec.pNativeApp);
        this->window->setWindowIcon("assets/icons/android_robot.png");
        this->window->setTheme(Theme::DARK);

        currentScene = std::make_unique<PBRTestScene>(renderer);

        if (currentScene) {
            currentScene->onLoad(NULL);
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
                if (currentScene) {
                    currentScene->onUpdate(deltaTime);
                }

                imGui.beginFrame();
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
