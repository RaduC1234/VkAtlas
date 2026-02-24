#pragma once

#ifdef ATLAS_PLATFORM_ANDROID

#include <game-activity/native_app_glue/android_native_app_glue.h>
#include "core/Window.hpp"
#include <vulkan/vulkan_core.h>

namespace Atlas {
    class AndroidWindow : public Window {
    public:
        AndroidWindow(const WindowSpecification& properties);

        bool shouldClose() override;
        void createWindowSurface(VkInstance instance, VkSurfaceKHR *surface) const override;
        void pollEvents() override;
        void waitEvents() override;
        std::vector<const char *> getRequiredExtensions() override;

        void *getNativeHandle() const override;

    private:
        android_app* app;

        void handleAppCommand(int32_t cmd);
        static void CommandThunk(android_app* app, int32_t cmd);
    };
}

#endif
