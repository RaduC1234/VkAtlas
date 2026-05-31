#include "core/Application.hpp"


namespace Atlas {
    Application::Application(const ApplicationCreateInfo& specification) : specification_(std::move(specification)), renderer_(specification_.rendererCreateInfo), assetManager_(renderer_.resourceManager()) {
        renderer_.window().setWindowIcon("assets/icons/android_robot.png");
        renderer_.window().setTheme(Window::Theme::Dark);
    }

    Application::~Application() {
        layers_.clear();
    }

    void Application::run() {
        auto currentTime = std::chrono::high_resolution_clock::now();

        while (!renderer_.window().shouldClose()) {
            renderer_.window().pollEvents();

            auto newTime = std::chrono::high_resolution_clock::now();
            const float deltaTime = std::chrono::duration_cast<std::chrono::duration<float>>(newTime - currentTime).count();
            currentTime = newTime;

            assetManager_.update();

            FrameContext frame = renderer_.beginFrame();
            if (frame.graphicsCommandBuffer == VK_NULL_HANDLE) {
                continue;
            }

            for (const auto &layer: layers_) {
                layer->onUpdate(deltaTime);
            }

            for (const auto &layer: layers_) {
                layer->onRender(frame);
            }

            renderer_.endFrame();
        }

        vkDeviceWaitIdle(renderer_.device().device());
    }
}
