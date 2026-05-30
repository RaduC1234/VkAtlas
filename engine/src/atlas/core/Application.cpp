#include "core/Application.hpp"


namespace Atlas {
    Application::Application(const ApplicationCreateInfo& specification) : specification_(std::move(specification)), renderer_(specification_.rendererCreateInfo), assetManager_(renderer_.resourceManager()) {
        renderer_.window().setWindowIcon("assets/icons/android_robot.png");
        renderer_.window().setTheme(Window::Theme::Dark);

        if (specification_.enableImGui) {
            const auto overlayLoadOp = renderer_.createInfo.sceneOutputTarget == Renderer::SceneOutputTarget::Texture
                                           ? Renderer::OverlayLoadOp::Clear
                                           : Renderer::OverlayLoadOp::Load;
            imguiLayer = std::make_unique<ImGuiLayer>(
                renderer_.device(),
                renderer_.window(),
                renderer_.getOverlayRenderPass(overlayLoadOp),
                static_cast<uint32_t>(renderer_.getImageCount())
            );
        }
    }

    Application::~Application() {
        layers.clear();
        imguiLayer.reset();
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

            if (imguiLayer) {
                imguiLayer->beginFrame(specification_.enableDockspace);
            }

            for (const auto &layer: layers) {
                layer->onUpdate(deltaTime);
            }

            for (const auto &layer: layers) {
                layer->onRender(frame);
            }

            if (imguiLayer) {
                for (const auto &layer: layers) {
                    layer->onImGuiRender();
                }

                const auto overlayLoadOp = renderer_.createInfo.sceneOutputTarget == Renderer::SceneOutputTarget::Texture
                                               ? Renderer::OverlayLoadOp::Clear
                                               : Renderer::OverlayLoadOp::Load;
                imguiLayer->endFrame();
                renderer_.beginOverlayRenderPass(frame.graphicsCommandBuffer, overlayLoadOp);
                imguiLayer->render(frame.graphicsCommandBuffer);
                renderer_.endOverlayRenderPass(frame.graphicsCommandBuffer);
            }

            renderer_.endFrame();
        }

        vkDeviceWaitIdle(renderer_.device().device());
    }
}
