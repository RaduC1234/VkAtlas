#include "Application.hpp"

// std
#include <chrono>

#include "asset/AssetManager.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "scene/OfficeScene.hpp"

namespace Atlas {
    Application::Application(const ApplicationSpecification &specification) : specification(specification) {
        AssetManager::create(this->device, specification.pNativeApp);
        this->window->setWindowIcon("assets/icons/android_robot.png");
        this->window->setTheme(Theme::DARK);

        currentScene = std::make_unique<OfficeScene>(renderer);

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
        auto currentTime = std::chrono::high_resolution_clock::now();

        while (!window->shouldClose()) {
            window->pollEvents();

            auto newTime = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration_cast<std::chrono::duration<float> >(newTime - currentTime).count();
            currentTime = newTime;

            renderer.beginCompute();
            if (currentScene) {
                currentScene->onUpdate(deltaTime);
            }
            renderer.endCompute();

            if (auto commandBuffer = renderer.beginFrame()) {
                if (currentScene) {
                    currentScene->onRender(commandBuffer);
                }

                renderer.endFrame();
            }
        }

        vkDeviceWaitIdle(device.device());
    }
} // namespace
