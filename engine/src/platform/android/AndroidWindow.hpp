#pragma once

#ifdef __ANDROID__

#include <game-activity/native_app_glue/android_native_app_glue.h>
#include "core/Window.hpp"

namespace Atlas {
    class AndroidWindow : public Window {
    public:
        AndroidWindow(const WindowSpecification& properties);

        bool shouldClose() override;
        void createWindowSurface(VkInstance instance, VkSurfaceKHR *surface) const override;
        void pollEvents() override;
        void waitEvents() override;
        std::vector<const char *> getRequiredExtensions() override;
    private:
        android_app* app;
    };
}

#endif
