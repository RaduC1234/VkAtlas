#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <vector>

#include <vulkan/vulkan.h>

#include "Core.hpp"

namespace Atlas {
    enum : uint32_t {
        ATLAS_PROPERTIES_WINDOW_UNDECORATED = BIT(0),
        ATLAS_PROPERTIES_WINDOW_DECORATED = BIT(1),
        ATLAS_PROPERTIES_WINDOW_RESIZEABLE = BIT(2),
        ATLAS_PROPERTIES_WINDOW_NON_RESIZEABLE = BIT(3),
    };

    struct WindowSpecification {
        void *pNativeApp = nullptr;
        uint32_t width = 1080;
        uint32_t height = 720;
        std::string title = "Atlas Window";
        uint32_t properties = ATLAS_PROPERTIES_WINDOW_DECORATED | ATLAS_PROPERTIES_WINDOW_RESIZEABLE;
    };

    class Window {
    public:
        virtual ~Window() = default;
        Window &operator=(const Window &) = delete;

        virtual bool shouldClose() = 0;
        virtual void createWindowSurface(VkInstance instance, VkSurfaceKHR *surface) const = 0;
        virtual void pollEvents() = 0;
        virtual void waitEvents() = 0;
        virtual std::vector<const char*> getRequiredExtensions() = 0;

        bool wasWindowResized() const { return framebufferResized; }
        void resetWindowResizedFlag() { this->framebufferResized = false; }

        VkExtent2D getExtent() const { return {width, height}; }
        int32_t getWidth() const { return width; }
        int32_t getHeight() const { return height; }

        static std::unique_ptr<Window> create(const WindowSpecification& specification);

    protected:
        uint32_t width{}, height{};
        bool framebufferResized{false};
    };
}
