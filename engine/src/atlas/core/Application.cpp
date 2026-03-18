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
    Application::Application(const ApplicationSpecification &specification) : specification(specification) {

        if (specification.renderMode != RenderMode::XROnly) {
            this->window = Window::create({specification.pNativeApp, 1200, 800});
            this->window->setWindowIcon("assets/icons/android_robot.png");
            this->window->setTheme(Theme::DARK);
        }

        this->device = std::make_unique<Device>(*this->window, specification.renderMode);
        this->renderer = std::make_unique<Renderer>(*this->window, *this->device);
        AssetManager::create(*this->device, specification.pNativeApp);

        currentScene = std::make_unique<OfficeScene>(*renderer);

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
        ImGuiLayer imGui{*device, *window, renderer->getSwapChainRenderPass(), static_cast<uint32_t>(renderer->getImageCount())};

        auto currentTime = std::chrono::high_resolution_clock::now();

        while (!window->shouldClose()) {
            window->pollEvents();

            auto newTime = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration_cast<std::chrono::duration<float> >(newTime - currentTime).count();
            currentTime = newTime;

            float aspect = renderer->getAspectRatio();

            renderer->beginCompute();
            if (currentScene) {
                currentScene->onUpdate(deltaTime);
            }
            renderer->endCompute();

            if (auto commandBuffer = renderer->beginFrame()) {
                imGui.beginFrame();
                renderer->beginSwapChainRenderPass(commandBuffer);

                if (currentScene) {
                    currentScene->onRender(deltaTime, aspect);
                }

                imGui.endFrame(commandBuffer);

                renderer->endSwapChainRenderPass(commandBuffer);
                renderer->endFrame();
            }
        }

        vkDeviceWaitIdle(device->device());
    }
} // namespace
