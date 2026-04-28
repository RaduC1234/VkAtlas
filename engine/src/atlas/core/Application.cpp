#include "Application.hpp"

// std
#include <chrono>

#include "asset/AssetManager.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "scene/OfficeScene.hpp"

namespace Atlas {
    Application::Application(const ApplicationSpecification &specification) : specification(specification), renderer(Renderer::Settings{.windowSettings = Window::Settings{.title = specification.name}}) {
        AssetManager::create(renderer.device(), specification.pNativeApp);
        this->renderer.window().setWindowIcon("assets/icons/android_robot.png");
        this->renderer.window().setTheme(Window::Theme::DARK);

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

        while (!renderer.window().shouldClose()) {
            renderer.window().pollEvents();

            auto newTime = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration_cast<std::chrono::duration<float> >(newTime - currentTime).count();
            currentTime = newTime;

            FrameContext frame = renderer.beginFrame();
            if (frame.graphicsCommandBuffer == VK_NULL_HANDLE)
                continue;

            if (currentScene) {
                currentScene->onUpdate(deltaTime);
                currentScene->onRender(frame);
            }

            renderer.endFrame();
        }

        vkDeviceWaitIdle(renderer.device().device());
    }
} // namespace
