#include "core/Application.hpp"

#include "core/Profiler.hpp"

namespace Atlas {
    Application::Application(const ApplicationCreateInfo& specification) : specification_(std::move(specification)), renderer_(specification_.rendererCreateInfo), assetManager_(renderer_.resourceManager()) {
        ATLAS_PROFILE_FUNCTION();
        renderer_.window().setWindowIcon("assets/icons/android_robot.png");
        renderer_.window().setTheme(Window::Theme::Dark);
    }

    Application::~Application() {
        ATLAS_PROFILE_FUNCTION();
        layers_.clear();
    }

    void Application::run() {
        ATLAS_PROFILE_THREAD("Atlas Main");
        ATLAS_PROFILE_FUNCTION();

        auto currentTime = std::chrono::high_resolution_clock::now();

        while (!renderer_.window().shouldClose()) {
            ATLAS_PROFILE_SCOPE("Application::Frame");

            {
                ATLAS_PROFILE_SCOPE("Window::pollEvents");
                renderer_.window().pollEvents();
            }

            auto newTime = std::chrono::high_resolution_clock::now();
            const float deltaTime = std::chrono::duration_cast<std::chrono::duration<float>>(newTime - currentTime).count();
            currentTime = newTime;

            {
                ATLAS_PROFILE_SCOPE("AssetManager::update");
                assetManager_.update();
            }

            FrameContext frame{};
            {
                ATLAS_PROFILE_SCOPE("Renderer::beginFrame");
                frame = renderer_.beginFrame();
            }
            if (frame.graphicsCommandBuffer == VK_NULL_HANDLE) {
                continue;
            }

            {
                ATLAS_PROFILE_SCOPE("LayerStack::onUpdate");
                for (const auto &layer: layers_) {
                    layer->onUpdate(deltaTime);
                }
            }

            {
                ATLAS_PROFILE_SCOPE("LayerStack::onRender");
                for (const auto &layer: layers_) {
                    layer->onRender(frame);
                }
            }

            {
                ATLAS_PROFILE_SCOPE("Renderer::endFrame");
                renderer_.endFrame();
            }

            ATLAS_PROFILE_FRAME();
        }

        ATLAS_PROFILE_SCOPE("Application::waitIdle");
        vkDeviceWaitIdle(renderer_.device().device());
    }
}
