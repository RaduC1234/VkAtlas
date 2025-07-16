#pragma once

#include <cassert>
#include <cstdint>
#include <string>
#include <memory>
#include <vector>

#include <vulkan/vulkan.h>

#include "Core.hpp"
#include "Mouse.hpp"

namespace Atlas {
    enum : uint32_t {
        WINDOW_PROPERTIES_UNDECORATED = BIT(0),
        WINDOW_PROPERTIES_DECORATED = BIT(1),
        WINDOW_PROPERTIES_RESIZEABLE = BIT(2),
        WINDOW_PROPERTIES_NON_RESIZEABLE = BIT(3),
    };

    enum CursorMode : uint32_t {
        WINDOW_CURSOR_DISABLED, // Hides and grabs the cursor, providing virtual and unlimited cursor movement. This is useful for implementing for example 3D camera controls.
        WINDOW_CURSOR_NORMAL, // Makes the cursor visible and behaving normally
        ATLAS_WINDOW_CURSOR_HIDDEN, // Makes the cursor invisible when it is over the content area of the window but does not restrict the cursor from leaving.
    };

    struct WindowSpecification {
        void *pNativeApp = nullptr;
        uint32_t width = 1080;
        uint32_t height = 720;
        std::string title = "Atlas Window";
        uint32_t properties = WINDOW_PROPERTIES_DECORATED | WINDOW_PROPERTIES_RESIZEABLE;
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

        virtual void setCursorMode(CursorMode cursorMode) { assert(true && "Method not implemented"); }

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
