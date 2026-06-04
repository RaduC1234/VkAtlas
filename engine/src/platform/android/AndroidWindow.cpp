#include "AndroidWindow.hpp"

#ifdef ATLAS_PLATFORM_ANDROID

#include "AndroidInputProvider.hpp"
#include "core/Log.hpp"
#include <vulkan/vulkan_android.h>

namespace Atlas {

    AndroidWindow::AndroidWindow(const WindowSpecification &spec) : app(reinterpret_cast<android_app *>(spec.pNativeApp)) {

        app->userData = this;
        app->onAppCmd = CommandThunk;

        if (app->window) {
            this->width = static_cast<uint32_t>(ANativeWindow_getWidth(app->window));
            this->height = static_cast<uint32_t>(ANativeWindow_getHeight(app->window));
            framebufferResized = true;
            AT_INFO("Android window created with size: {}x{}", this->width, this->height);
        } else {
            AT_WARN("AndroidWindow: ANativeWindow not ready yet. Waiting for APP_CMD_INIT_WINDOW.");
        }
    }

    void AndroidWindow::CommandThunk(android_app* app, int32_t cmd) {
        auto* window = static_cast<AndroidWindow*>(app->userData);
        if (window) {
            window->handleAppCommand(cmd);
        }
    }

    void AndroidWindow::handleAppCommand(int32_t cmd) {
        switch (cmd) {
            case APP_CMD_INIT_WINDOW:
                if (app->window) {
                    this->width = static_cast<uint32_t>(ANativeWindow_getWidth(app->window));
                    this->height = static_cast<uint32_t>(ANativeWindow_getHeight(app->window));
                    framebufferResized = true;
                    AT_INFO("APP_CMD_INIT_WINDOW: Framebuffer size set to {}x{}", this->width, this->height);
                }
                break;
            case APP_CMD_WINDOW_RESIZED:
                framebufferResized = true;
                AT_INFO("APP_CMD_WINDOW_RESIZED: Resize flagged");
                break;
            case APP_CMD_TERM_WINDOW:
            AT_INFO("APP_CMD_TERM_WINDOW: Window is being destroyed");
                break;
        }
    }

    bool AndroidWindow::shouldClose() {
        return app->destroyRequested != 0;
    }

    void AndroidWindow::createWindowSurface(VkInstance instance, VkSurfaceKHR *surface) const {
        VkAndroidSurfaceCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        createInfo.window = app->window;

        if (vkCreateAndroidSurfaceKHR(instance, &createInfo, nullptr, surface) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Android Vulkan surface!");
        }

        AT_INFO("Vulkan surface created successfully on Android.");
    }

    void AndroidWindow::pollEvents() {
        int events;
        android_poll_source *source;

        while (ALooper_pollOnce(0, nullptr, &events, reinterpret_cast<void **>(&source)) >= 0) {
            if (source) {
                source->process(app, source);
            }
            if (shouldClose()) break;
        }

        AndroidInputProvider::instance().processEvents(app);
    }

    void AndroidWindow::waitEvents() {
        int events;
        android_poll_source *source;

        // Wait indefinitely for next event (blocks)
        while (ALooper_pollOnce(-1, nullptr, &events, reinterpret_cast<void **>(&source)) >= 0) {
            if (source) {
                source->process(app, source);
            }
            if (shouldClose()) break;

            // If window becomes valid during wait
            if (app->window != nullptr && width == 0 && this->height == 0) {
                this->width = static_cast<uint32_t>(ANativeWindow_getWidth(app->window));
                this->height = static_cast<uint32_t>(ANativeWindow_getHeight(app->window));
                framebufferResized = true;
                AT_INFO("waitEvents: Detected valid window size {}x{}", this->width, this->height);
            }
        }
    }

    std::vector<const char *> AndroidWindow::getRequiredExtensions() {
        return {
                "VK_KHR_surface",
                "VK_KHR_android_surface"
        };
    }

    void *AndroidWindow::getNativeHandle() const {
        return this->app;
    }

}
#endif
