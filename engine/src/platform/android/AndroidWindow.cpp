#include "AndroidWindow.hpp"

#ifdef __ANDROID__

#include "core/Log.hpp"
#include <vulkan/vulkan_android.h>

namespace Atlas {

    AndroidWindow::AndroidWindow(const WindowSpecification &spec) : app(reinterpret_cast<android_app*>(spec.pNativeApp)) {
        AT_INFO("Android window created with size: " + std::to_string(spec.width) + "x" + std::to_string(spec.height));
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
        android_poll_source* source;

        while (ALooper_pollOnce(0, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
            if (source) {
                source->process(app, source);
            }
            if (app->destroyRequested) break;
        }
    }

    std::vector<const char*> AndroidWindow::getRequiredExtensions() {
        return {
                "VK_KHR_surface",
                "VK_KHR_android_surface"
        };
    }

}
#endif
