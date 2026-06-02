#include "core/Application.hpp"
#include "core/Profiler.hpp"

namespace Atlas {
    Application::Application(const ApplicationCreateInfo& specification) : specification_(std::move(specification)), renderer_(specification_.rendererCreateInfo), assetManager_(renderer_.resourceManager()) {
        renderer_.window().setWindowIcon("assets/icons/android_robot.png");
        renderer_.window().setTheme(Window::Theme::Dark);
    }

    Application::~Application() {
        layers_.clear();
    }

    void Application::run() {
        ATLAS_PROFILE_THREAD("Main Thread");
        auto currentTime = std::chrono::high_resolution_clock::now();

        while (!renderer_.window().shouldClose()) {
            ATLAS_PROFILE_SCOPE("Application::frame");

            {
                ATLAS_PROFILE_SCOPE("Application::pollEvents");
                renderer_.window().pollEvents();
            }

            auto newTime = std::chrono::high_resolution_clock::now();
            const float deltaTime = std::chrono::duration_cast<std::chrono::duration<float>>(newTime - currentTime).count();
            currentTime = newTime;

            if (specification_.onFrame) {
                ATLAS_PROFILE_SCOPE("Application::onFrame");
                specification_.onFrame(renderer_.window(), deltaTime);
            }

            {
                ATLAS_PROFILE_SCOPE("Application::assetUpdate");
                assetManager_.update();
            }

            FrameContext frame = renderer_.beginFrame();
            if (frame.graphicsCommandBuffer == VK_NULL_HANDLE) {
                continue;
            }

            for (const auto &layer: layers_) {
                const std::string zoneName = layer->getName() + "::onUpdate";
                ATLAS_PROFILE_SCOPE_DYNAMIC(zoneName.c_str());
                layer->onUpdate(deltaTime);
            }

            for (const auto &layer: layers_) {
                const std::string zoneName = layer->getName() + "::onRender";
                ATLAS_PROFILE_SCOPE_DYNAMIC(zoneName.c_str());
                layer->onRender(frame);
            }

            {
                ATLAS_PROFILE_SCOPE("Application::endFrame");
                renderer_.endFrame();
            }
            ATLAS_PROFILE_FRAME();
        }

        vkDeviceWaitIdle(renderer_.device().device());
    }
}
