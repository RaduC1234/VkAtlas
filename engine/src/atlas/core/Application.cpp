#include "core/Application.hpp"

#include <chrono>

#if defined(ATLAS_PLATFORM_ANDROID)
#include "android/AndroidAssetManager.hpp"
#elif defined(ATLAS_PLATFORM_DESKTOP)
#include "desktop/DesktopAssetManager.hpp"
#endif

namespace Atlas {
    Application::Application(ApplicationSpecification specification) : specification_(std::move(specification)), renderer_(specification_.rendererSettings) {
#if defined(ATLAS_PLATFORM_ANDROID)
        assetManager_ = std::make_unique<AndroidAssetManager>(
            renderer_.device(),
            specification_.rendererSettings.windowSettings.pNativeApp
        );
#elif defined(ATLAS_PLATFORM_DESKTOP)
        assetManager_ = std::make_unique<DesktopAssetManager>(
            renderer_.device(),
            specification_.rendererSettings.windowSettings.pNativeApp
        );
#else
#error Unsupported platform for AssetManager
#endif

        renderer_.window().setWindowIcon("assets/icons/android_robot.png");
        renderer_.window().setTheme(Window::Theme::DARK);

        if (specification_.enableImGui) {
            const auto overlayLoadOp = renderer_.settings.sceneOutputTarget == Renderer::SceneOutputTarget::Texture
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

                const auto overlayLoadOp = renderer_.settings.sceneOutputTarget == Renderer::SceneOutputTarget::Texture
                                               ? Renderer::OverlayLoadOp::Clear
                                               : Renderer::OverlayLoadOp::Load;
                imguiLayer->endFrame();
                renderer_.beginOverlayRenderPass(frame.graphicsCommandBuffer, overlayLoadOp);
                imguiLayer->renderDrawData(frame.graphicsCommandBuffer);
                renderer_.endOverlayRenderPass(frame.graphicsCommandBuffer);
            }

            renderer_.endFrame();
        }

        vkDeviceWaitIdle(renderer_.device().device());
    }
}
